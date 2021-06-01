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

#include <stdlib.h>
#include <stdio.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <unistd.h>

static struct FragTable* ft;
static struct SharedData* st;
static struct SharedData* slb;
static int tun_fd = -1;
struct fragStats* sft;

#define CNTINC(x) __atomic_add_fetch(&(x),1,__ATOMIC_RELAXED)

#ifdef VERBOSE
#include "time.h"
#include "limiter.h"
#define D(x)
#define Dx(x) x
static void printFragStats(struct timespec* now)
{
	struct fragStats stats;
	fragGetStats(ft, now, &stats);
	fragPrintStats(&stats);
}
static void printIpv6FragStats(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	static struct limiter* l = NULL;
	if (l == NULL)
		l = limiterCreate(100, 1100);
	if (limiterGo(&now, l))
		printFragStats(&now);
}
#else
#define D(x)
#define Dx(x)
#endif

static void injectFragFn(void const* data, unsigned len)
{
	int rc = write(tun_fd, data, len);
	if (rc == len)
		CNTINC(sft->fragsInjected);
	Dx(printf("Injected frag, len=%u, rc=%d\n", len, rc));
}



static int handleIpv4(void* payload, unsigned plen)
{
	struct iphdr* hdr = (struct iphdr*)payload;
	unsigned hash = 0;

	if (ntohs(hdr->frag_off) & (IP_OFFMASK|IP_MF)) {
		// Make an addres-hash and check if we shall forward to the LB tier
		hash = ipv4AddressHash(payload, plen);
		int fw = slb->magd.lookup[hash % slb->magd.M];
		if (fw >= 0 && fw != slb->ownFwmark) {
			Dx(printf("IPv4 fragment to LB tier. fw=%d\n", fw));
			return fw + slb->fwOffset; /* To the LB tier */
		}

		// We shall handle the frament here
		int rc = ipv4HandleFragment(ft, payload, plen, &hash, injectFragFn);
		if (rc != 0) {
			Dx(printf("IPv4 fragment dropped or stored, rc=%d\n", rc));
			return -1;
		}
		Dx(printf(
			   "Handle IPv4 frag locally hash=%u, fwmark=%u\n",
			   hash, st->magd.lookup[hash % st->magd.M] + st->fwOffset));
	} else {
		switch (hdr->protocol) {
		case IPPROTO_TCP:
		case IPPROTO_UDP:
			hash = ipv4TcpUdpHash(payload, plen);
			break;
		case IPPROTO_ICMP:
			hash = ipv4IcmpHash(payload, plen);
			break;
		case IPPROTO_SCTP:
		default:;
		}
	}
	return st->magd.lookup[hash % st->magd.M] + st->fwOffset;
}

static int handleIpv6(void* payload, unsigned plen)
{
	unsigned hash;

	struct ip6_hdr* hdr = (struct ip6_hdr*)payload;
	/*
	  TODO; the next-header does NOT have to be the fragment-header!!
	  See; https://datatracker.ietf.org/doc/html/rfc2460#section-4.1
	 */
	if (hdr->ip6_nxt == IPPROTO_FRAGMENT) {

		// Make an addres-hash and check if we shall forward to the LB tier
		hash = ipv6AddressHash(payload, plen);
		int fw = slb->magd.lookup[hash % slb->magd.M];
		if (fw >= 0 && fw != slb->ownFwmark) {
			Dx(printf("IPv6 fragment to LB tier. fw=%d\n", fw));
			return fw + slb->fwOffset; /* To the LB tier */
		}

		// We shall handle the frament here
		int rc = ipv6HandleFragment(ft, payload, plen, &hash, injectFragFn);
		if (rc != 0) {
			Dx(printf("IPv6 fragment dropped or stored, rc=%d\n", rc));
			Dx(printIpv6FragStats());
			return -1;
		}
		Dx(printf(
			   "Handle IPv6 frag locally hash=%u, fwmark=%u\n",
			   hash, st->magd.lookup[hash % st->magd.M] + st->fwOffset));
		Dx(printIpv6FragStats());
	} else {
		hash = ipv6Hash(payload, plen);
	}
	return st->magd.lookup[hash % st->magd.M] + st->fwOffset;
}

static int packetHandleFn(
	unsigned short proto, void* payload, unsigned plen)
{
	int fw = st->fwOffset;
	switch (proto) {
	case ETH_P_IP:
		fw = handleIpv4(payload, plen);
		break;
	case ETH_P_IPV6:
		fw = handleIpv6(payload, plen);
		break;
	default:;
		// We should not get here because ip(6)tables handles only ip (4/6)
		Dx(printf("Unexpected protocol 0x%04x\n", proto));
		fw = -1;
	}
	Dx(printf("Packet; len=%u, fw=%d\n", plen, fw));
	return fw;
}

static int cmdLb(int argc, char **argv)
{
	char const* targetShm = defaultTargetShm;
	char const* lbShm = defaultLbShm;
	char const* ftShm = "ftshm";
	char const* qnum = "2";
	char const* ft_size = "500";
	char const* ft_buckets = "500";
	char const* ft_frag = "100";
	char const* ft_ttl = "200";
	char const* dev;
	char const* tun = NULL;
	struct Option options[] = {
		{"help", NULL, 0,
		 "lb [options]\n"
		 "  Load-balance"},
		{"dev", &dev, REQUIRED, "Ingress device"},
		{"tun", &tun, 0, "Tun device for re-inject fragments"},
		{"tshm", &targetShm, 0, "Target shared memory"},
		{"lbshm", &lbShm, 0, "Lb shared memory"},
		{"queue", &qnum, 0, "NF-queue to listen to (default 2)"},
		{"ft_shm", &ftShm, 0, "Frag table; shared memory stats"},
		{"ft_size", &ft_size, 0, "Frag table; size"},
		{"ft_buckets", &ft_buckets, 0, "Frag table; extra buckets"},
		{"ft_frag", &ft_frag, 0, "Frag table; stored frags"},
		{"ft_ttl", &ft_ttl, 0, "Frag table; ttl milliS"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);
	st = mapSharedDataOrDie(targetShm,sizeof(*st), O_RDONLY);
	slb = mapSharedDataOrDie(lbShm,sizeof(*slb), O_RDONLY);
	// Create and re-map the stats struct
	sft = calloc(1, sizeof(*sft));
	createSharedDataOrDie(ftShm, sft, sizeof(*sft));
	free(sft);
	sft = mapSharedDataOrDie(ftShm, sizeof(*sft), O_RDWR);

	// Get MTU from the ingress device
	int mtu = get_mtu(dev);
	if (mtu < 576)
		die("Could not get a valid MTU from dev [%s]\n", dev);

	/* Open the "tun" device if specified. Check that the mtu is at
	 * least as large as for the ingress device */
	if (tun != NULL) {
		tun_fd = tun_alloc(tun, IFF_TUN|IFF_NO_PI);
		if (tun_fd < 0)
			die("Failed to open tun device [%s]\n", tun);
		int tun_mtu = get_mtu(tun);
		if (tun_mtu < mtu)
			die("Tun mtu too small; %d < %d\n", tun_mtu, mtu);
	}

	ft = fragInit(
		atoi(ft_size),		/* table size */
		atoi(ft_buckets),	/* Extra buckets for hash collisions */
		atoi(ft_frag),		/* Max stored fragments */
		mtu,				/* MTU + some extras */
		atoi(ft_ttl));		/* Fragment TTL in milli seconds */
	fragUseStats(ft, sft);
	printf(
		"FragTable; size=%d, buckets=%d, frag=%d, mtu=%d, ttl=%d\n",
		atoi(ft_size),atoi(ft_buckets),atoi(ft_frag),mtu,atoi(ft_ttl));
	return nfqueueRun(atoi(qnum), packetHandleFn);
}

__attribute__ ((__constructor__)) static void addCommands(void) {
	addCmd("lb", cmdLb);
}
