/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

#include "nfqueue.h"
#include <iputils.h>
#include <shmem.h>
#include <cmd.h>
#include <die.h>
#include <tuntap.h>
#include <maglevdyn.h>
#include <reassembler.h>

#include <stdlib.h>
#include <stdio.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

static struct FragTable* ft;
static struct SharedData* st;
static struct SharedData* slb = NULL;
static int tun_fd = -1;
static struct fragStats* sft;
static struct MagDataDyn magd;
static struct MagDataDyn magdlb;
static unsigned udpEncap;

#ifdef VERBOSE
#define D(x)
#define Dx(x) x
#else
#define D(x)
#define Dx(x)
#endif

#define FW(table) table.active[table.lookup[hash % table.M]]

static void injectFrag(void const* data, unsigned len)
{
#ifdef VERBOSE
	if (tun_fd >= 0) {
		int rc = write(tun_fd, data, len);
		printf("Frag injected, len=%u, rc=%d\n", len, rc);
	} else {
		printf("Frag dropped, len=%u\n", len);
	}
#else
	if (tun_fd >= 0)
		write(tun_fd, data, len);
#endif
}


static int packetHandleFn(
	unsigned short proto, void* data, unsigned len)
{
	struct ctKey key;
	uint64_t fragid;
	int rc = getHashKey(&key, udpEncap, &fragid, proto, data, len);
	if (rc < 0)
		return -1;

	unsigned hash;
	if (rc & 3) {
		// Fragment. Check if we shall forward to the lb-tier
		if (slb != NULL) {
			hash = hashKeyAddresses(&key);
			int fw = FW(magdlb);
			if (fw >= 0 && fw != slb->ownFwmark) {
				Dx(printf("Fragment to LB tier. fw=%d\n", fw));
				return fw; /* To the LB tier */
			}
		}

		// We shall handle the fragment here
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		if (rc & 1) {
			hash = hashKey(&key);
			key.id = fragid;
			if (handleFirstFragment(ft, &now, &key, hash, data, len) != 0)
				return -1;
		} else {
			rc = fragGetHashOrStore(ft, &now, &key, &hash, data, len);
			if (rc != 0) {
				Dx(printf("Fragment %s\n", rc > 0 ? "stored":"dropped"));
				return -1;
			}
			Dx(printf(
				   "Handle frag locally hash=%u, fwmark=%u\n",hash, FW(magd)));
		}
	} else {
		hash = hashKey(&key);
	}

	Dx(printf("Packet; len=%u, fw=%d\n", len, FW(magd)));
	return FW(magd);
}

static void *packetHandleThread(void* Q)
{
	nfqueueRun((intptr_t)Q);
	return NULL;
}

static int cmdLb(int argc, char **argv)
{
	char const* targetShm = defaultTargetShm;
	char const* lbShm = NULL;
	char const* ftShm = "ftshm";
	char const* qnum = "2";
	char const* qlen = "1024";
	char const* ft_size = "500";
	char const* ft_buckets = "500";
	char const* ft_frag = "100";
	char const* ft_ttl = "200";
	char const* mtuOpt = "1500";
	char const* tun = NULL;
	char const* reassembler = "0";
	char const* sctpEncap = "0";
	struct Option options[] = {
		{"help", NULL, 0,
		 "lb [options]\n"
		 "  Load-balance"},
		{"mtu", &mtuOpt, 0, "MTU. At least the mtu of the ingress device"},
		{"tun", &tun, 0, "Tun device for re-inject fragments"},
		{"reassembler", &reassembler, 0, "Reassembler size. default=0"},
		{"sctp_encap", &sctpEncap, 0, "SCTP UDP encapsulation port. default=0"},
		{"tshm", &targetShm, 0, "Target shared memory"},
		{"lbshm", &lbShm, 0, "Lb shared memory"},
		{"queue", &qnum, 0, "NF-queues to listen to (default 2)"},
		{"qlength", &qlen, 0, "Lenght of queues (default 1024)"},
		{"ft_shm", &ftShm, 0, "Frag table; shared memory stats"},
		{"ft_size", &ft_size, 0, "Frag table; size"},
		{"ft_buckets", &ft_buckets, 0, "Frag table; extra buckets"},
		{"ft_frag", &ft_frag, 0, "Frag table; stored frags"},
		{"ft_ttl", &ft_ttl, 0, "Frag table; ttl milliS"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);
	st = mapSharedDataOrDie(targetShm, O_RDONLY);
	magDataDyn_map(&magd, st->mem);
	if (lbShm != NULL) {
		slb = mapSharedDataOrDie(lbShm, O_RDONLY);
		magDataDyn_map(&magdlb, slb->mem);
	}
	// Create and re-map the stats struct
	sft = calloc(1, sizeof(*sft));
	createSharedDataOrDie(ftShm, sft, sizeof(*sft));
	free(sft);
	sft = mapSharedDataOrDie(ftShm, O_RDWR);

	// Get MTU from the ingress device
	int mtu = atoi(mtuOpt);
	if (mtu < 576)
		die("Invalid MTU; %d\n", mtu);

	// SCTP encapsulation. 0 - No encapsulation (default)
	udpEncap = atoi(sctpEncap);

	/* Open the "tun" device if specified. Check that the mtu is at
	 * least as large as for the ingress device */
	if (tun != NULL) {
		tun_fd = tun_alloc(tun, IFF_TUN|IFF_NO_PI);
		if (tun_fd < 0)
			die("Failed to open tun device [%s]\n", tun);
		int tun_mtu = get_mtu(tun);
		if (tun_mtu < mtu)
			die("Tun mtu too small; %d < %d\n", tun_mtu, mtu);
		setInjectFn(injectFrag);
	} else {
		/*
		  We can't inject stored fragments. Disable storing of
		  fragments. The MTU *may* be set to to 1280 to avoid copy unnecessary
		  data to user-space, but then reassembly will not work since the
		  original packet lenght is lost.
		 */
		ft_frag = "0";
	}

	ft = fragTableCreate(
		atoi(ft_size),		/* table size */
		atoi(ft_buckets),	/* Extra buckets for hash collisions */
		atoi(ft_frag),		/* Max stored fragments */
		mtu,				/* MTU. Only used for stored fragments */
		atoi(ft_ttl));		/* Fragment TTL in milli seconds */
	fragUseStats(ft, sft);
	if (atoi(reassembler) > 0)
		fragRegisterFragReassembler(ft, createReassembler(atoi(reassembler)));
	printf(
		"FragTable; size=%d, buckets=%d, frag=%d, mtu=%d, ttl=%d\n",
		atoi(ft_size),atoi(ft_buckets),atoi(ft_frag),mtu,atoi(ft_ttl));

	nfqueueInit(packetHandleFn, atoi(qlen), mtu);

	/*
	  The qnum may be a range like "0:3" in which case we go
	  multi-threading and listen to all queues in the range;
	 */
	unsigned Q = atoi(qnum);
	if (strchr(qnum, ':') != NULL) {
		unsigned first, last;
		if (sscanf(qnum, "%u:%u", &first, &last) != 2)
			die("queue invalid [%s]\n", qnum);
		if (first > last)
			die("queue invalid [%s]\n", qnum);
		for (Q = first; Q < last; Q++) {
			pthread_t tid;
			if (pthread_create(&tid, NULL, packetHandleThread, (void*)(intptr_t)Q) != 0)
				die("Failed pthread_create for Q=%u\n", Q);
		}
		Q = last;				/* Last will go to the main thread */
	}
	return nfqueueRun(Q);
}

__attribute__ ((__constructor__)) static void addCommands(void) {
	addCmd("lb", cmdLb);
}


