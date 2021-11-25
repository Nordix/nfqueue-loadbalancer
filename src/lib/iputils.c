/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

#include "iputils.h"
#include <hash.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip6.h>
#include <netinet/ether.h>
#include <netinet/icmp6.h>
#include <netinet/udp.h>

#ifdef VERBOSE
#include "stdio.h"
#define Dx(x) x
#else
#define Dx(x)
#endif
#define D(x)

int ipv6IsExtensionHeader(unsigned hdr)
{
	switch (hdr) {
	case IPPROTO_HOPOPTS:
	case IPPROTO_ROUTING:
	case IPPROTO_FRAGMENT:
	case IPPROTO_DSTOPTS:
		return 1;
	}
	// We can't handle AH or ESP headers
	return 0;
}


static inline void keySetAddr4(struct ctKey* key, struct iphdr* hdr)
{
	key->src.s6_addr16[5] = 0xffff;
	key->src.s6_addr32[3] = hdr->saddr;
	key->dst.s6_addr16[5] = 0xffff;
	key->dst.s6_addr32[3] = hdr->daddr;
}

// Get the HashKey from the "inner" header in an ICMP reply
// Prerequisite; the packet is icmp with an inner header
static int getInnerHashKeyIpv4(
	struct ctKey* key, unsigned udpEncap, struct icmphdr* ihdr,
	void const* data, unsigned len)
{
	void const* endp = data + len;
	int rc = 8;

	/*
	  The original datagram is found on offset 8 in the icmp
	  header. Since the original datagram is outgoing we must
	  switch src<->dst for both addresses and ports.
	 */
	struct iphdr* hdr = (void*)ihdr + 8;
	if (!IN_BOUNDS(hdr, sizeof(*hdr), endp) || hdr->ihl < 5)
		return -1;

	// (swapped!)
	key->src.s6_addr16[5] = 0xffff;
	key->src.s6_addr32[3] = hdr->daddr;
	key->dst.s6_addr16[5] = 0xffff;
	key->dst.s6_addr32[3] = hdr->saddr;
	key->ports.proto = hdr->protocol;

	switch (hdr->protocol) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_SCTP:
		break;
	default:
		return rc + 32;			/* Only addresses */
	}

	uint16_t const* ports = (uint16_t const*)((uint32_t*)hdr + hdr->ihl);
	if (!IN_BOUNDS(ports, sizeof(uint16_t) * 2, endp))
		return -1;
	
	// Check if we have a udp-encapsulated sctp packet.
	// NOTE; we must check the sport!
	if (udpEncap != 0) {
		if (hdr->protocol == IPPROTO_UDP && ntohs(ports[0]) == udpEncap) {
			ports += 4;			/* Skip the udp header */
			if (!IN_BOUNDS(ports, sizeof(uint16_t) * 2, endp))
				return -1;
			key->ports.proto = IPPROTO_SCTP;
			rc += 4;
		}
	}

	// (swapped!)
	key->ports.src = ports[1];
	key->ports.dst = ports[0];
	D(printf("getInnerHashKeyIpv4: rc=%d, hash=%u\n", rc, hashKey(key)));
	return rc;
}

static int getHashKeyIpv4(
	struct ctKey* key, unsigned udpEncap, uint64_t* fragid,
	void const* data, unsigned len)
{
	void const* endp = data + len;
	int rc = 0;
	struct iphdr* hdr = (struct iphdr*)data;

	if (!IN_BOUNDS(hdr, sizeof(*hdr), endp))
		return -1;				/* Truncated packet */
	if (hdr->ihl < 5)
		return -1;				/* Invalid packet */

	if (ntohs(hdr->frag_off) & (IP_OFFMASK|IP_MF)) {
		// Fragment
		if ((ntohs(hdr->frag_off) & IP_OFFMASK) == 0) {
			// First fragment
			if (fragid != NULL)
				*fragid = hdr->id;
			rc += 1;
		} else {
			keySetAddr4(key, hdr);
			key->id = hdr->id;
			return 2;			/* <-- Non-first frag */
		}
	}

	if (hdr->protocol == IPPROTO_ICMP) {
		struct icmphdr* ihdr = (struct icmphdr*)((uint32_t*)data + hdr->ihl);
		switch (ihdr->type) {
		case ICMP_DEST_UNREACH:
		case ICMP_SOURCE_QUENCH:
		case ICMP_REDIRECT:
		case ICMP_TIME_EXCEEDED:
			if (rc & 1)
				return -1;		/* A fragmented icmp reply */
			return getInnerHashKeyIpv4(key, udpEncap, ihdr, data, len);
		default:;
		}
		keySetAddr4(key, hdr);
		if (ihdr->type == ICMP_ECHO)
			key->id = ihdr->un.echo.id;
		else
			key->ports.proto = IPPROTO_ICMP;
		return rc + 16;			/* <-- Normal ICMP return */
	}

	// We have a non-icmp message, possibly first-fragment. Addresses
	// are OK.
	keySetAddr4(key, hdr);
	key->ports.proto = hdr->protocol;
	switch (hdr->protocol) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_SCTP:
		break;
	default:
		return rc + 32;			/* Only addresses */
	}

	uint16_t const* ports = (uint16_t const*)((uint32_t*)data + hdr->ihl);
	if (!IN_BOUNDS(ports, sizeof(uint16_t) * 2, endp))
		return -1;
	
	// Check if we have a udp-encapsulated sctp packet.
	if (udpEncap != 0) {
		if (hdr->protocol == IPPROTO_UDP && ntohs(ports[1]) == udpEncap) {
			ports += 4;			/* Skip the udp header */
			if (!IN_BOUNDS(ports, sizeof(uint16_t) * 2, endp))
				return -1;
			key->ports.proto = IPPROTO_SCTP;
			rc += 4;
		}
	}

	key->ports.src = ports[0];
	key->ports.dst = ports[1];
	D(printf("getHashKeyIpv4: rc=%d, hash=%u\n", rc, hashKey(key)));
	return rc;
}

// Get the HashKey from the "inner" header in an ICMP reply
// Prerequisite; the packet is icmp with an inner header
static int getInnerHashKeyIpv6(
	struct ctKey* key, unsigned udpEncap, struct icmp6_hdr const* ihdr,
	void const* data, unsigned len)
{
	void const* endp = data + len;
	int rc = 8;

	/*
	  The original datagram is found on offset 8 in the icmp
	  header. Since the original datagram is outgoing we must
	  switch src<->dst for both addresses and ports.
	 */
	struct ip6_hdr* ip6hdr = (void*)ihdr + 8;
	if (!IN_BOUNDS(ip6hdr, sizeof(*ip6hdr), endp))
		return -1;

	uint8_t htype = ip6hdr->ip6_nxt;
	void const* hdr = (void*)ip6hdr + sizeof(struct ip6_hdr);
	while (ipv6IsExtensionHeader(htype)) {
		struct ip6_ext const* xh = hdr;
		if (!IN_BOUNDS(xh, sizeof(*xh), endp))
			return -1;
		htype = xh->ip6e_nxt;
		if (xh->ip6e_len == 0)
			return -1;			/* Corrupt header */
		hdr = hdr + (xh->ip6e_len * 8);
	}

	// (swapped!)
	key->dst = ip6hdr->ip6_src;
	key->src = ip6hdr->ip6_dst;
	key->ports.proto = htype;

	switch (htype) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_SCTP:
		break;
	default:
		return rc + 32;			/* Only addresses */
	}

	uint16_t const* ports = (uint16_t const*)hdr;
	if (!IN_BOUNDS(ports, sizeof(uint16_t) * 2, endp))
		return -1;

	// Check if we have a udp-encapsulated sctp packet.
	// NOTE; we must check the sport!
	if (udpEncap != 0) {
		if (htype == IPPROTO_UDP && ntohs(ports[0]) == udpEncap) {
			ports += 4;			/* Skip the udp header */
			if (!IN_BOUNDS(ports, sizeof(uint16_t) * 2, endp))
				return -1;
			key->ports.proto = IPPROTO_SCTP;
			rc += 4;
		}
	}

	// (swapped!)
	key->ports.src = ports[1];
	key->ports.dst = ports[0];
	D(printf("getInnerHashKeyIpv6: rc=%d, hash=%u\n", rc, hashKey(key)));
	return rc;
}

static int getHashKeyIpv6(
	struct ctKey* key, unsigned udpEncap, uint64_t* fragid,
	void const* data, unsigned len)
{
	void const* endp = data + len;
	int rc = 0;

	struct ip6_hdr* ip6hdr = (struct ip6_hdr*)data;
	if (!IN_BOUNDS(ip6hdr, sizeof(*ip6hdr), endp))
		return -1;

	uint8_t htype = ip6hdr->ip6_nxt;
	void const* hdr = data + sizeof(struct ip6_hdr);
	while (ipv6IsExtensionHeader(htype)) {
		if (htype == IPPROTO_FRAGMENT)
			break;
		struct ip6_ext const* xh = hdr;
		if (!IN_BOUNDS(xh, sizeof(*xh), endp))
			return -1;
		htype = xh->ip6e_nxt;
		if (xh->ip6e_len == 0)
			return -1;			/* Corrupt header */
		hdr = hdr + (xh->ip6e_len * 8);
	}
	
	if (htype == IPPROTO_FRAGMENT) {
		struct ip6_frag const* fh = hdr;
		if ((fh->ip6f_offlg & IP6F_OFF_MASK) == 0) {
			// First fragment
			if (fragid != NULL)
				*fragid = fh->ip6f_ident;
			while (ipv6IsExtensionHeader(htype)) {
				struct ip6_ext const* xh = hdr;
				htype = xh->ip6e_nxt;
				hdr = hdr + (xh->ip6e_len * 8);
			}
			rc += 1;
		} else {
			key->dst = ip6hdr->ip6_dst;
			key->src = ip6hdr->ip6_src;
			key->id = fh->ip6f_ident;
			return 2;			/* <-- Non-first frag */
		}
	}

	if (htype == IPPROTO_ICMPV6) {
		struct icmp6_hdr const* ih = hdr;
		switch (ih->icmp6_type) {
		case ICMP6_DST_UNREACH:
		case ICMP6_PACKET_TOO_BIG:
			// TODO; More types here?
			if (rc & 1)
				return -1;		/* A fragmented icmp reply */
			return getInnerHashKeyIpv6(key, udpEncap, ih, data, len);
		default:;
		}
		key->dst = ip6hdr->ip6_dst;
		key->src = ip6hdr->ip6_src;
		if (ih->icmp6_type == ICMP6_ECHO_REQUEST)
			key->id = ih->icmp6_id;
		else
			key->ports.proto = IPPROTO_ICMPV6;
		return rc + 16;			/* <-- Normal ICMP return */		
	}

	// We have a non-icmp message, possibly first-fragment. Addresses
	// are OK.
	key->dst = ip6hdr->ip6_dst;
	key->src = ip6hdr->ip6_src;
	key->ports.proto = htype;
	switch (htype) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_SCTP:
		break;
	default:
		return rc + 32;			/* Only addresses */
	}

	uint16_t const* ports = (uint16_t const*)hdr;
	if (!IN_BOUNDS(ports, sizeof(uint16_t) * 2, endp))
		return -1;

	// Check if we have a udp-encapsulated sctp packet.
	if (udpEncap != 0) {
		if (htype == IPPROTO_UDP && ntohs(ports[1]) == udpEncap) {
			ports += 4;			/* Skip the udp header */
			if (!IN_BOUNDS(ports, sizeof(uint16_t) * 2, endp))
				return -1;
			key->ports.proto = IPPROTO_SCTP;
			rc += 4;
		}
	}

	key->ports.src = ports[0];
	key->ports.dst = ports[1];
	D(printf("getHashKeyIpv6: rc=%d, hash=%u (%u,%u)\n", rc, hashKey(key), ntohs(ports[0]), ntohs(ports[1])));
	return rc;
}

int getHashKey(
	struct ctKey* key, unsigned udpEncap, uint64_t* fragid,
	unsigned proto, void const* data, unsigned len)
{
	memset(key, 0, sizeof(*key));

	switch (proto) {
	case ETH_P_IP:
		return getHashKeyIpv4(key, udpEncap, fragid, data, len);
	case ETH_P_IPV6:
		return getHashKeyIpv6(key, udpEncap, fragid, data, len);
	default:;
		// We should not get here because ip(6)tables handles only ip (4/6)
	}
	return -1;
}

unsigned hashKey(struct ctKey* key)
{
	if (key->ports.proto == IPPROTO_SCTP)
		return HASH(&key->ports.src, sizeof(uint16_t) * 2);
	return HASH(key, sizeof(*key));
}
unsigned hashKeyAddresses(struct ctKey* key)
{
	return HASH(key, sizeof(struct in6_addr) * 2);
}
