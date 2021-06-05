/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

#include "nfqueue.h"
#include <fragutils.h>
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
#include <time.h>

static struct FragTable* ft;
static struct SharedData* st;
static struct SharedData* slb = NULL;
static int tun_fd = -1;
struct fragStats* sft;

#ifdef VERBOSE
#define D(x)
#define Dx(x) x
#else
#define D(x)
#define Dx(x)
#endif


static int ipv6HandleFragment(
	struct FragTable* ft, void const* data, unsigned len,
	struct ip6_frag const* fh, unsigned* hash);
static int ipv4HandleFragment(
	struct FragTable* ft, void const* data, unsigned len, unsigned* hash);


static int handleIpv4(void* data, unsigned len)
{
	struct iphdr* hdr = (struct iphdr*)data;
	unsigned hash = 0;

	if (ntohs(hdr->frag_off) & (IP_OFFMASK|IP_MF)) {
		// Make an addres-hash and check if we shall forward to the LB tier
		if (slb != NULL) {
			hash = ipv4AddressHash(data, len);
			int fw = slb->magd.lookup[hash % slb->magd.M];
			if (fw >= 0 && fw != slb->ownFwmark) {
				Dx(printf("IPv4 fragment to LB tier. fw=%d\n", fw));
				return fw + slb->fwOffset; /* To the LB tier */
			}
		}

		// We shall handle the frament here
		int rc = ipv4HandleFragment(ft, data, len, &hash);
		if (rc != 0) {
			Dx(printf("IPv4 fragment %s\n", rc > 0 ? "stored":"dropped"));
			return -1;
		}
		Dx(printf(
			   "Handle IPv4 frag locally hash=%u, fwmark=%u\n",
			   hash, st->magd.lookup[hash % st->magd.M] + st->fwOffset));
	} else {
		hash = ipv4Hash(data, len);
	}
	return st->magd.lookup[hash % st->magd.M] + st->fwOffset;
}

static int handleIpv6(void const* data, unsigned len)
{
	unsigned hash;

	/*
	  Find the fragment header or the upper-layer header
	  https://datatracker.ietf.org/doc/html/rfc2460#section-4.1
	 */
	struct ip6_hdr* ip6hdr = (struct ip6_hdr*)data;
	uint8_t htype = ip6hdr->ip6_nxt;
	void const* hdr = data + sizeof(struct ip6_hdr);
	while (ipv6IsExtensionHeader(htype)) {
		if (htype == IPPROTO_FRAGMENT)
			break;
		struct ip6_ext const* xh = hdr;
		htype = xh->ip6e_nxt;
		hdr = hdr + (xh->ip6e_len * 8);
		// TODO: Check that we don't step outside the packet!
	}

	if (htype == IPPROTO_FRAGMENT) {

		// Do we have an lb-tier?
		if (slb != NULL) {
			// Make an addres-hash and check if we shall forward to the LB tier
			hash = ipv6AddressHash(data, len);
			int fw = slb->magd.lookup[hash % slb->magd.M];
			if (fw >= 0 && fw != slb->ownFwmark) {
				Dx(printf("IPv6 fragment to LB tier. fw=%d\n", fw));
				return fw + slb->fwOffset; /* To the LB tier */
			}
		}

		// We shall handle the frament here
		int rc = ipv6HandleFragment(ft, data, len, hdr, &hash);
		if (rc != 0) {
			Dx(printf("IPv6 fragment %s\n", rc > 0 ? "stored":"dropped"));
			return -1;
		}
		Dx(printf(
			   "Handle IPv6 frag locally hash=%u, fwmark=%u\n",
			   hash, st->magd.lookup[hash % st->magd.M] + st->fwOffset));
	} else {
		hash = ipv6Hash(data, len, htype, hdr);
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
	char const* lbShm = NULL;
	char const* ftShm = "ftshm";
	char const* qnum = "2";
	char const* ft_size = "500";
	char const* ft_buckets = "500";
	char const* ft_frag = "100";
	char const* ft_ttl = "200";
	char const* mtuOpt = "1500";
	char const* tun = NULL;
	struct Option options[] = {
		{"help", NULL, 0,
		 "lb [options]\n"
		 "  Load-balance"},
		{"mtu", &mtuOpt, 0, "MTU. At least the mtu of the ingress device"},
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
	if (lbShm != NULL)
		slb = mapSharedDataOrDie(lbShm,sizeof(*slb), O_RDONLY);
	// Create and re-map the stats struct
	sft = calloc(1, sizeof(*sft));
	createSharedDataOrDie(ftShm, sft, sizeof(*sft));
	free(sft);
	sft = mapSharedDataOrDie(ftShm, sizeof(*sft), O_RDWR);

	// Get MTU from the ingress device
	int mtu = atoi(mtuOpt);
	if (mtu < 576)
		die("Invalid MTU; %d\n", mtu);

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

	ft = fragTableCreate(
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


static int ipv4HandleFragment(
	struct FragTable* ft, void const* data, unsigned len, unsigned* hash)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	// Construct the key
	struct ctKey key = {0};
	struct iphdr* hdr = (struct iphdr*)data;
	key.src.s6_addr16[5] = 0xffff;
	key.src.s6_addr32[3] = hdr->saddr;
	key.dst.s6_addr16[5] = 0xffff;
	key.dst.s6_addr32[3] = hdr->daddr;
	key.id = hdr->id;

	// Check offset to see if this is the first fragment
	if ((ntohs(hdr->frag_off) & IP_OFFMASK) == 0) {
		// First fragment. contains the protocol header.
		*hash = ipv4Hash(data, len);

		struct Item* storedFragments;
		if (fragInsertFirst(ft, &now, &key, *hash, &storedFragments) != 0) {
			itemFree(storedFragments);
			return -1;
		}

		/* Check if we have any stored fragments that should be
		 * re-injected */
		if (storedFragments != NULL) {
			struct Item* i;
			for (i = storedFragments; i != NULL; i = i->next) {
				injectFrag(i->data, i->len);
			}
			itemFree(storedFragments);
		}
		return 0;				/* First fragment handled. Hash stored. */
	}

	/*
	  Not the first fragment. Get the hash if possible or store this
	  fragment if not.
	*/

	return fragGetHashOrStore(ft, &now, &key, hash, data, len);
}

#define PAFTER(x) (void*)x + (sizeof(*x))

static int ipv6HandleFragment(
	struct FragTable* ft, void const* data, unsigned len,
	struct ip6_frag const* fh, unsigned* hash)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	// Construct the key
	struct ctKey key;
	struct ip6_hdr* ip6hdr = (struct ip6_hdr*)data;
	key.dst = ip6hdr->ip6_dst;
	key.src = ip6hdr->ip6_src;
	key.id = fh->ip6f_ident;

	// Check offset to see if this is the first fragment
	uint16_t fragOffset = (fh->ip6f_offlg & IP6F_OFF_MASK) >> 3;
	if (fragOffset == 0) {
		/* First fragment. Find the upper layer header. */
		uint8_t htype = ip6hdr->ip6_nxt;
		void const* hdr = data + sizeof(struct ip6_hdr);
		while (ipv6IsExtensionHeader(htype)) {
			struct ip6_ext const* xh = hdr;
			htype = xh->ip6e_nxt;
			hdr = hdr + (xh->ip6e_len * 8);
		}
		*hash = ipv6Hash(data, len, htype, hdr);

		struct Item* storedFragments;
		if (fragInsertFirst(ft, &now, &key, *hash, &storedFragments) != 0) {
			itemFree(storedFragments);
			return -1;
		}

		/* Check if we have any stored fragments that should be
		 * re-injected */
		if (storedFragments != NULL) {
			struct Item* i;
			for (i = storedFragments; i != NULL; i = i->next) {
				injectFrag(i->data, i->len);
			}
			itemFree(storedFragments);
		}
		return 0;				/* First fragment handled. Hash stored. */
	}

	/*
	  Not the first fragment. Get the hash if possible or store this
	  fragment if not.
	*/

	return fragGetHashOrStore(ft, &now, &key, hash, data, len);
}
