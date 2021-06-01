/*
   SPDX-License-Identifier: Apache-2.0
   Copyright (c) 2021 Nordix Foundation
*/

#include "iputils.h"
#include <time.h>
#include <netinet/ip.h>


int ipv4HandleFragment(
	struct FragTable* ft, void const* data, unsigned len, unsigned* hash,
	injectFragFn_t injectFragFn)
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
		switch (hdr->protocol) {
		case IPPROTO_TCP:		/* (should not happen?) */
		case IPPROTO_UDP:
			*hash = ipv4TcpUdpHash(data, len);
			break;
		case IPPROTO_ICMP:
			*hash = ipv4IcmpHash(data, len);
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

