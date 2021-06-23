/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

#include "iputils.h"
#include <hash.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>

#ifdef VERBOSE
#include "stdio.h"
#define Dx(x) x
#else
#define Dx(x)
#endif
#define D(x)


static unsigned ipv4TcpUdpHash(void const* data, unsigned len)
{
	// We hash on addresses and ports
	struct iphdr* hdr = (struct iphdr*)data;
	if (hdr->ihl < 5)
		return ipv4AddressHash(data, len); /* Corrupt packet */
	uint32_t const* ports = (uint32_t*)data + hdr->ihl;
	if (!IN_BOUNDS(ports, sizeof(*ports), data + len))
		return ipv4AddressHash(data, len);

	uint32_t hashData[3];
	hashData[0] = hdr->saddr;
	hashData[1] = hdr->daddr;
	hashData[2] = *ports;
	return HASH(hashData, sizeof(hashData));
}
static uint32_t flip16(uint32_t v)
{
	uint16_t* p = (uint16_t*)&v;
	return (p[0] << 16) + p[1];
}
static unsigned ipv4IcmpInnerHash(void const* data, unsigned len)
{
	void const* endp = data + len;

	/*
	  We must use the *inner* header to make sure the origial sender
	  gets the reply.
	*/
	struct iphdr* hdr = (struct iphdr*)data;
	struct icmphdr* ihdr = (struct icmphdr*)((uint32_t*)data + hdr->ihl);
	Dx(printf(
		   "ipv4IcmpInnerHash; len=%u, code=%u, mtu=%u\n",
		   len, ihdr->code, ntohs(ihdr->un.frag.mtu)));

	/*
	  The original datagram is found on offset 8 in this icmp
	  header. But since the original datagram is outgoing we must
	  switch src<->dst for both addresses and port before we calculate
	  the hash.
	 */
	hdr = (void*)ihdr + 8;
	if (hdr->ihl < 5)
		return ipv4AddressHash(data, len);
	if (!IN_BOUNDS(hdr, sizeof(*hdr), endp))
		return ipv4AddressHash(data, len);

	uint32_t hashData[3];
	hashData[0] = hdr->daddr;
	hashData[1] = hdr->saddr;
	uint32_t const* ports = (uint32_t*)hdr + hdr->ihl;

	switch (hdr->protocol) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		if (!IN_BOUNDS(ports, sizeof(*ports), endp))
			return ipv4AddressHash(data, len);
		hashData[2] = flip16(*ports);
		D(printf(
			  "  %u %08x %08x %08x\n", hdr->protocol,
			  ntohl(hashData[0]), ntohl(hashData[1]), ntohl(hashData[2])));
		return HASH(hashData, sizeof(hashData));
	default:;
	}
	// Hash on flipped addresses
	return HASH(hashData, sizeof(uint32_t) * 2);
}
static unsigned ipv4IcmpHash(void const* data, unsigned len)
{
	struct iphdr* hdr = (struct iphdr*)data;
	if (hdr->ihl < 5)
		return ipv4AddressHash(data, len); /* Corrupt packet */
	struct icmphdr* ihdr = (struct icmphdr*)((uint32_t*)data + hdr->ihl);
	if (!IN_BOUNDS(ihdr, sizeof(*ihdr), data + len))
		return ipv4AddressHash(data, len);
	
	D(printf("ipv4IcmpHash; type=%u\n", ihdr->type));
	switch (ihdr->type) {
	case ICMP_ECHO: {
		// We hash on addresses and id
		uint32_t hashData[3];
		hashData[0] = hdr->saddr;
		hashData[1] = hdr->daddr;
		hashData[2] = ihdr->un.echo.id;
		return HASH((uint8_t const*)hashData, sizeof(hashData));
	}
	case ICMP_DEST_UNREACH:
	case ICMP_SOURCE_QUENCH:
	case ICMP_REDIRECT:
	case ICMP_TIME_EXCEEDED:
		return ipv4IcmpInnerHash(data, len);
	default:;
	}
	return ipv4AddressHash(data, len);
}
unsigned ipv4Hash(void const* data, unsigned len)
{
	struct iphdr* hdr = (struct iphdr*)data;
	switch (hdr->protocol) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		return ipv4TcpUdpHash(data, len);
	case IPPROTO_ICMP:
		return ipv4IcmpHash(data, len);
	case IPPROTO_SCTP:
		break;
	default:;
	}
	return ipv4AddressHash(data, len);
}

unsigned ipv4AddressHash(void const* data, unsigned len)
{
	struct iphdr* hdr = (struct iphdr*)data;
	return HASH((uint8_t const*)&hdr->saddr, 8);
}

 

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

static unsigned
ipv6TcpUdpHash(struct ip6_hdr const* h, uint32_t const* ports)
{
	int32_t hashData[9];
	memcpy(hashData, &h->ip6_src, 32);
	hashData[8] = *ports;
	return HASH(hashData, sizeof(hashData));
}

/*
  https://datatracker.ietf.org/doc/html/rfc4443#section-3.2
*/
static unsigned ipv6IcmpInnerHash(
	void const* data, unsigned len,
	struct ip6_hdr const* h, struct icmp6_hdr const* ih)
{
	void const* endp = data + len;

	/*
	  The original datagram is found on offset 8 in this icmp6
	  header. But since the original datagram is outgoing we must
	  switch src<->dst for both addresses and port before we calculate
	  the hash.
	 */
	h = (void*)ih + 8;			/* Now at the "inner" header */
	if (!IN_BOUNDS(h, sizeof(*h), endp))
		return ipv6AddressHash(data, len);
	
	/* Skip all extension headers */
	uint8_t htype = h->ip6_nxt;
	void const* hdr = (void*)h + sizeof(struct ip6_hdr);
	while (ipv6IsExtensionHeader(htype)) {
		struct ip6_ext const* xh = hdr;
		if (!IN_BOUNDS(xh, sizeof(*xh), endp))
			return ipv6AddressHash(data, len);
		htype = xh->ip6e_nxt;
		if (xh->ip6e_len == 0)
			return ipv6AddressHash(data, len);
		hdr = hdr + (xh->ip6e_len * 8);
	}

	Dx(printf("ipv6IcmpInnerHash; len=%u, inner-type=%u\n", len,htype));
	uint32_t hashData[9];
	memcpy(hashData, &h->ip6_dst, 16);
	memcpy(hashData + 4, &h->ip6_src, 16);

	switch (htype) {
	case IPPROTO_TCP:
	case IPPROTO_UDP: {
		/* Reverse addresses and ports and hash */
		uint32_t const* ports = (uint32_t const*)hdr;
		if (IN_BOUNDS(ports, sizeof(*ports), endp)) {
			hashData[8] = flip16(*ports);
			D(printf("Ports %08x\n", ntohl(hashData[8])));
			return HASH(hashData, sizeof(hashData));
		}
	}
	case IPPROTO_SCTP:
	default:;
	}
	// Hash on inner (flipped) addresses by default
	return HASH(hashData, sizeof(uint32_t) * 8);
}
static unsigned
ipv6IcmpHash(
	void const* data, unsigned len,
	struct ip6_hdr const* h, struct icmp6_hdr const* ih)
{
	D(printf("ipv6IcmpHash; type=%u\n", ih->icmp6_type));
	switch (ih->icmp6_type) {
	case ICMP6_DST_UNREACH:
	case ICMP6_PACKET_TOO_BIG:
		// TODO; More types here?
		return ipv6IcmpInnerHash(data, len, h, ih);
	case ICMP6_ECHO_REQUEST: {
		if (!IN_BOUNDS(ih, sizeof(*ih), data + len))
			ipv6AddressHash(data, len);
		int32_t hashData[9];
		memcpy(hashData, &h->ip6_src, 32);
		hashData[8] = ih->icmp6_id;
		return HASH(hashData, sizeof(hashData));
	}
	default:;
	}
	return ipv6AddressHash(data, len);
}

unsigned ipv6Hash(
	void const* data, unsigned len, unsigned htype, void const* hdr)
{
	D(printf("ipv6Hash; type=%u\n", htype));
	struct ip6_hdr* ip6hdr = (struct ip6_hdr*)data;

	switch (htype) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		if (!IN_BOUNDS(hdr, 4, data + len))
			return -1;
		return ipv6TcpUdpHash(ip6hdr, hdr);
	case IPPROTO_ICMPV6:
		return ipv6IcmpHash(data, len, ip6hdr, hdr);
	case IPPROTO_SCTP:
		break;
	default:;
	}
	return ipv6AddressHash(data, len);
}
unsigned ipv6AddressHash(void const* data, unsigned len)
{
	// Including the flow-label
	struct ip6_hdr const* hdr = data;
	uint32_t hdata[9];
	memcpy(hdata, &hdr->ip6_src, 32);
	hdata[8] = ntohl(hdr->ip6_flow) & 0xfffff;
	return HASH(hdata, sizeof(hdata));
}
