/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021-2022 Nordix Foundation
*/

#include "nfqlb.h"

#include <nfqueue.h>
#include <shmem.h>
#include <cmd.h>
#include <tuntap.h>
#include <maglevdyn.h>
#include <reassembler.h>
#include <log.h>

#include <stdlib.h>
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
static unsigned hash_mode;
static int notargets_fw = -1;

#ifdef VERBOSE
#define D(x)
#define Dx(x) x
#else
#define D(x)
#define Dx(x)
#endif

static void injectFrag(void const* data, unsigned len)
{
	if (tun_fd >= 0) {
		int rc = write(tun_fd, data, len);
		trace(TRACE_FRAG, "Frag injected, len=%u, rc=%d\n", len, rc);
		if (rc != len)
			warning("FAILED: injectFrag write(%u), rc=%d\n", len, rc);
	} else {
		trace(TRACE_FRAG, "Frag dropped, len=%u\n", len);
	}
}


static int packetHandleFn(
	unsigned short proto, void* data, unsigned len)
{
	struct ctKey key;
	uint64_t fragid;
	int rc = getHashKey(&key, udpEncap, &fragid, proto, data, len, hash_mode);
	if (rc < 0)
		return -1;

	unsigned hash;
	int fw;
	if (rc & 3) {
		// Fragment. Check if we shall forward to the lb-tier
		if (slb != NULL) {
			hash = hashKeyAddresses(&key);
			fw = magdlb.lookup[hash % magdlb.M];
			if (fw >= 0)
				fw = magdlb.active[fw];
			if (fw >= 0 && fw != slb->ownFwmark) {
				trace(TRACE_FRAG, "Fragment to LB tier. fw=%d\n", fw);
				return fw; /* To the LB tier */
			}
		}

		// We shall handle the fragment here
		if ((rc & 1) == 0) {
			// Not first-fragment
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			rc = fragGetValueOrStore(ft, &now, &key, &fw, data, len);
			if (rc != 0) {
				trace(TRACE_FRAG, "Fragment %s\n", rc > 0 ? "stored":"dropped");
				return -1;
			}
			trace(TRACE_FRAG, "Handle non-first frag locally fwmark=%d\n", fw);
			return fw;
		}
	}

	hash = hashKey(&key, hash_mode);
	fw = magd.lookup[hash % magd.M];
	if (fw >= 0)
		fw = magd.active[fw];
	if (fw < 0)
		return notargets_fw;

	if (rc & 1) {
		// First fragment
		trace(TRACE_FRAG, "First fragment\n");
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		key.id = fragid;
		if (handleFirstFragment(ft, &now, &key, fw, data, len) != 0) {
			trace(TRACE_FRAG, "FAILED: Handle first fragment\n");
			return -1;
		}
	}

	TRACE(TRACE_PACKET){
		tracef("Load-balance packet. fw=%d\n", fw);
	}
	return fw;
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
	char const* notargets_fwmark = "-1";
	char const* lb_hash_mode = "1";
	char const* trace_address = DEFAULT_TRACE_ADDRESS;
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
		{"notargets_fwmark", &notargets_fwmark, 0, "Set when there are no targets"},
		{"hash_mode", &lb_hash_mode, 0, "Load balance with a different hash mode. 0: Tuple-5, 1: SCTP Ports only. default=1"},
		{"queue", &qnum, 0, "NF-queues to listen to (default 2)"},
		{"qlength", &qlen, 0, "Lenght of queues (default 1024)"},
		{"ft_shm", &ftShm, 0, "Frag table; shared memory stats"},
		{"ft_size", &ft_size, 0, "Frag table; size"},
		{"ft_buckets", &ft_buckets, 0, "Frag table; extra buckets"},
		{"ft_frag", &ft_frag, 0, "Frag table; stored frags"},
		{"ft_ttl", &ft_ttl, 0, "Frag table; ttl milliS"},
		{"trace_address",  &trace_address, 0, "Trace server address"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);
	logConfigShm(TRACE_SHM);
	logTraceServer(trace_address);

	st = mapSharedDataOrDie(targetShm, O_RDONLY);
	magDataDyn_map(&magd, st->mem);
	if (lbShm != NULL) {
		slb = mapSharedDataOrDie(lbShm, O_RDONLY);
		magDataDyn_map(&magdlb, slb->mem);
	}
	notargets_fw = atoi(notargets_fwmark);

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

	hash_mode = atoi(lb_hash_mode);

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


