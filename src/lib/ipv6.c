/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

#include "iputils.h"
#include "hash.h"
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <pthread.h>

#define PAFTER(x) (void*)x + (sizeof(*x))

int ipv6HandleFragment(
	struct FragTable* ft, void const* data, unsigned len, unsigned* hash,
	injectFragFn_t injectFragFn)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	// Construct the key
	struct ctKey key;
	struct ip6_hdr* hdr = (struct ip6_hdr*)data;
	struct ip6_frag* fh = (struct ip6_frag*)(data + 40);
	key.dst = hdr->ip6_dst;
	key.src = hdr->ip6_src;
	key.id = fh->ip6f_ident;

	// Check offset to see if this is the first fragment
	uint16_t fragOffset = (fh->ip6f_offlg & IP6F_OFF_MASK) >> 3;
	if (fragOffset == 0) {
		// First fragment. contains the protocol header.
		switch (fh->ip6f_nxt) {
		case IPPROTO_TCP:		/* (should not happen?) */
		case IPPROTO_UDP:
			*hash = ipv6TcpUdpHash(hdr, PAFTER(fh));
			break;
		case IPPROTO_ICMPV6:
			*hash = ipv6IcmpHash(hdr, PAFTER(fh));
			break;
		case IPPROTO_SCTP:
		default:
			*hash = 0;
		}
		if (fragInsertFirst(ft, &now, &key, *hash) != 0) {
			return -1;
		}

		/* Check if we have any stored fragments that should be
		 * re-injected */
		struct Item* storedFragments = fragGetStored(ft, &now, &key);
		if (storedFragments != NULL) {
			if (injectFragFn != NULL) {
				struct Item* i;
				for (i = storedFragments; i != NULL; i = i->next) {
					injectFragFn(i->data, i->len);
				}
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
