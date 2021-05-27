/*
   SPDX-License-Identifier: Apache-2.0
   Copyright (c) 2021 Nordix Foundation
*/

#include "iputils.h"
#include "fragutils.h"
#include "hash.h"
#include <stdio.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/ip_icmp.h>
#include <netinet/ether.h>

#define Dx(x) x
#define D(x)

char const* protocolString(unsigned p)
{
	static char buf[8];
	switch (p) {
	case IPPROTO_ICMP: return "ICMP";
	case IPPROTO_TCP: return "TCP";
	case IPPROTO_UDP: return "UDP";
	case IPPROTO_ICMPV6: return "ICMPV6";
	default:
		sprintf(buf, "%u", p);
	}
	return buf;
}

void ipv4Print(unsigned len, uint8_t const* pkt)
{
	struct iphdr* hdr = (struct iphdr*)pkt;
	char src[16], dst[16];
	char const* frag = "";
	if (ntohs(hdr->frag_off) & (IP_OFFMASK|IP_MF))
		frag = "frag";
	printf(
		"  IPv4: %s -> %s, %s %s\n",
		inet_ntop(AF_INET, &hdr->saddr, src, sizeof(src)),
		inet_ntop(AF_INET, &hdr->daddr, dst, sizeof(dst)),
		protocolString(hdr->protocol), frag);
}

unsigned ipv4TcpUdpHash(void const* data, unsigned len)
{
	// We hash on addresses and ports
	struct iphdr* hdr = (struct iphdr*)data;
	int32_t hashData[3];
	hashData[0] = hdr->saddr;
	hashData[1] = hdr->daddr;
	hashData[2] = *((uint32_t*)data + hdr->ihl);
	return HASH((uint8_t const*)hashData, sizeof(hashData));
}
unsigned ipv4IcmpHash(void const* data, unsigned len)
{
	struct iphdr* hdr = (struct iphdr*)data;
	struct icmphdr* ihdr = (struct icmphdr*)((uint32_t*)data + hdr->ihl);
	int32_t hashData[3];
	hashData[0] = hdr->saddr;
	hashData[1] = hdr->daddr;
	switch (ihdr->type) {
	case ICMP_ECHO:
		// We hash on addresses and id
		hashData[2] = ihdr->un.echo.id;
		break;
	case ICMP_FRAG_NEEDED:
		// This is a PMTU discovery reply. We must use the *inner*
		// header to make sure the origial sender gets the reply.
	default:
		hashData[2] = 0;
	}
	return HASH((uint8_t const*)hashData, sizeof(hashData));
}

unsigned ipv4AddressHash(void const* data, unsigned len)
{
	struct iphdr* hdr = (struct iphdr*)data;
	return HASH((uint8_t const*)&hdr->saddr, 8);
}

int ipv4HandleFragment(void const* data, unsigned len, unsigned* hash)
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
		if (fragInsertFirst(&now, &key, *hash) != 0) {
			return -1;
		}

		/* Check if we have any stored fragments that should be
		 * re-injected */
		struct Item* storedFragments = fragGetStored(&now, &key);
		if (storedFragments != NULL) {
			unsigned cnt = 0;
			for (struct Item* i = storedFragments; i != NULL; i = i->next)
				cnt++;
			printf("Dropped %u stored fragments\n", cnt);
			itemFree(storedFragments);
			return -1;	/* NYI */
		}
		return 0;				/* First fragment handled. Hash stored. */
	}

	/*
	  Not the first fragment. Get the hash if possible or store this
	  fragment if not.
	*/

	return fragGetHashOrStore(&now, &key, hash, data, len);
}

void framePrint(unsigned len, uint8_t const* pkt)
{
	if (len < sizeof(struct ethhdr)) {
		printf("Short packet %u\n", len);
		return;
	}
	struct ethhdr const* eth = (struct ethhdr const*)pkt;
	pkt += sizeof(struct ethhdr);
	len -= sizeof(struct ethhdr);
	uint16_t proto = htons(eth->h_proto);
	printf("%s -> %s; 0x%04x\n", macToString(eth->h_source), macToString(eth->h_dest), proto);
	switch (proto) {
	case ETH_P_IP:
		ipv4Print(len, pkt);
		break;
	case ETH_P_IPV6:
		ipv6Print(len, pkt);
		break;
	default:;
	}
}

