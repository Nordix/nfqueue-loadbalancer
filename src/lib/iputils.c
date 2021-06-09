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
	int32_t hashData[3];
	hashData[0] = hdr->saddr;
	hashData[1] = hdr->daddr;
	hashData[2] = *((uint32_t*)data + hdr->ihl);
	return HASH(hashData, sizeof(hashData));
}
static uint32_t flip16(uint32_t v)
{
	uint16_t* p = (uint16_t*)&v;
	return (p[0] << 16) + p[1];
}
static unsigned ipv4IcmpDestUnreachHash(void const* data, unsigned len)
{
	/*
	  Dest unreachable including PMTU discovery reply. We must use the
	  *inner* header to make sure the origial sender gets the reply.
	*/
	struct iphdr* hdr = (struct iphdr*)data;
	struct icmphdr* ihdr = (struct icmphdr*)((uint32_t*)data + hdr->ihl);
	D(printf(
		   "ipv4IcmpDestUnreachHash; code=%u, mtu=%u\n",
		   ihdr->code, ntohs(ihdr->un.frag.mtu)));

	/*
	  The original datagram is found on offset 8 in this icmp
	  header. But since the original datagram is outgoing we must
	  switch src<->dst for both addresses and port before we calculate
	  the hash.
	 */
	hdr = (void*)ihdr + 8;
	uint32_t hashData[3];
	hashData[0] = hdr->daddr;
	hashData[1] = hdr->saddr;
	hashData[2] = flip16(*((uint32_t*)hdr + hdr->ihl));
	D(printf(
		   "  %u %08x %08x %08x\n", hdr->protocol,
		   ntohl(hashData[0]), ntohl(hashData[1]), ntohl(hashData[2])));
	return HASH(hashData, sizeof(hashData));
}
static unsigned ipv4IcmpHash(void const* data, unsigned len)
{
	struct iphdr* hdr = (struct iphdr*)data;
	struct icmphdr* ihdr = (struct icmphdr*)((uint32_t*)data + hdr->ihl);
	int32_t hashData[3];
	hashData[0] = hdr->saddr;
	hashData[1] = hdr->daddr;
	Dx(printf("ipv4IcmpHash; type=%u\n", ihdr->type));
	switch (ihdr->type) {
	case ICMP_ECHO:
		// We hash on addresses and id
		hashData[2] = ihdr->un.echo.id;
		break;
	case ICMP_DEST_UNREACH:
		return ipv4IcmpDestUnreachHash(data, len);
	default:
		hashData[2] = 0;
	}
	return HASH((uint8_t const*)hashData, sizeof(hashData));
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
static unsigned ipv6IcmpDestUnreachHash(
	void const* data, unsigned len,
	struct ip6_hdr const* h, struct icmp6_hdr const* ih)
{
	/*
	  The original datagram is found on offset 8 in this icmp6
	  header. But since the original datagram is outgoing we must
	  switch src<->dst for both addresses and port before we calculate
	  the hash.
	 */
	h = (void*)ih + 8;			/* Now at the "inner" header */

	/* Skip all extension headers */
	uint8_t htype = h->ip6_nxt;
	void const* hdr = (void*)h + sizeof(struct ip6_hdr);
	while (ipv6IsExtensionHeader(htype)) {
		struct ip6_ext const* xh = hdr;
		htype = xh->ip6e_nxt;
		hdr = hdr + (xh->ip6e_len * 8);
		// TODO: Check that we don't step outside the packet!
	}

	// TODO; Don't assume tcp/udp
	D(printf("ipv6IcmpDestUnreachHash; inner-type=%u\n", htype));
	/* Reverse addresses and ports and hash */
	uint32_t hashData[9];
	memcpy(hashData, &h->ip6_dst, 16);
	memcpy(hashData + 4, &h->ip6_src, 16);
	hashData[8] = flip16(*((uint32_t*)hdr));
	D(printf("Ports %08x\n", ntohl(hashData[8])));
	return HASH(hashData, sizeof(hashData));

	return 0;
}
static unsigned
ipv6IcmpHash(
	void const* data, unsigned len,
	struct ip6_hdr const* h, struct icmp6_hdr const* ih)
{
	D(printf("ipv6IcmpHash; type=%u\n", ih->icmp6_type));
	int32_t hashData[9];
	switch (ih->icmp6_type) {
	case ICMP6_DST_UNREACH:
	case ICMP6_PACKET_TOO_BIG:
		return ipv6IcmpDestUnreachHash(data, len, h, ih);
	case ICMP6_ECHO_REQUEST:
		hashData[8] = ih->icmp6_id;
		break;
	default:
		hashData[8] = 0;
	}
	memcpy(hashData, &h->ip6_src, 32);
	return HASH(hashData, sizeof(hashData));
}

unsigned ipv6Hash(
	void const* data, unsigned len, unsigned htype, void const* hdr)
{
	D(printf("ipv6Hash; type=%u\n", htype));
	struct ip6_hdr* ip6hdr = (struct ip6_hdr*)data;
	switch (htype) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
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
