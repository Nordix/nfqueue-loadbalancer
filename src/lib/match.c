/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021-2022 Nordix Foundation
*/

#include <match.h>
#include <die.h>
#include <iputils.h>

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <regex.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>

#define D(x)
#define Dx(x)
#define CALLOC(n,x) calloc(n, sizeof(*(x))); if (x == NULL) die("OOM")


static regex_t regex;
#define REGEX "^(sctp|tcp|udp)\\[[0-9]+ *: *[124]\\]( *& *0x[0-9a-f]+)? *= *([0-9]+|0x[0-9a-f]+)$"
__attribute__ ((__constructor__)) static void compile_regexp(void) {
	regcomp(&regex, REGEX, REG_EXTENDED|REG_NOSUB);
}

struct MatchItem {
	unsigned proto;
	unsigned offset;
	unsigned nbytes;
	uint32_t mask;
	uint32_t value;
};

struct Match {
	unsigned itemCount;
	struct MatchItem* items;
};

static unsigned headerlength(unsigned l4proto)
{
	switch (l4proto) {
	case IPPROTO_SCTP:
		return 12;
	case IPPROTO_TCP:
		return 20;
	case IPPROTO_UDP:
		return 8;
	}
	return 0;
}

struct Match* matchCreate(void)
{
	struct Match* m = CALLOC(1,m);
	return m;
}

void matchDestroy(struct Match* match)
{
	if (match == NULL)
		return;
	free(match->items);
	free(match);
}
static char const* matchParse(char const* str, struct MatchItem* item)
{
	if (strlen(str) > 64)
		return "String too long";
	if (regexec(&regex, str, 0, NULL, 0) != 0)
		return "Doesn't match regexp";

	if (strncmp(str, "sctp", 4) == 0)
		item->proto = IPPROTO_SCTP;
	else if (strncmp(str, "tcp", 3) == 0)
		item->proto = IPPROTO_TCP;
	else if (strncmp(str, "udp", 3) == 0)
		item->proto = IPPROTO_UDP;
	else
		return "Unknown protocol";

	char const* cp;
	char* eptr;
	cp = strchr(str, '[');
	item->offset = strtol(cp+1, &eptr, 0);
	cp = strchr(eptr, ':');
	item->nbytes = strtol(cp+1, &eptr, 0);
	if (item->nbytes != 1 && item->nbytes != 2 && item->nbytes != 4)
		return "Nbytes invalid. Only 1,2,4 is allowed";

	// Check that offset + nbytes is within the L4 header
	if (item->offset + item->nbytes > headerlength(item->proto))
		return "Out of bounds";

	cp = strchr(eptr, '&');
	if (cp != NULL) {
		item->mask = strtol(cp+1, &eptr, 0);
	}
	cp = strchr(eptr, '=');
	item->value = strtol(cp+1, &eptr, 0);
	return NULL;
}

char const* matchValidate(char const* str)
{
	struct MatchItem item;
	return matchParse(str, &item);
}
char const* matchAdd(struct Match* match, char const* str)
{
	struct MatchItem item = {0};
	char const* err = matchParse(str, &item);
	if (err != NULL)
		return err;

	match->itemCount++;
	match->items = reallocarray(match->items, match->itemCount, sizeof(item));
	if (match->items == NULL)
		die("OOM");
	match->items[match->itemCount - 1] = item;

#if 0
	if (item.mask != 0)
		printf("%u, [%u,%u] & 0x%08x = %u\n", item.proto, item.offset, item.nbytes, item.mask, item.value);
	else
		printf("%u, [%u,%u] = %u\n", item.proto, item.offset, item.nbytes, item.value);
#endif
	return NULL;
}

unsigned matchItemCount(struct Match* match)
{
	if (match == NULL)
		return 0;
	return match->itemCount;
}

// Forwards
static int matchesItem(struct MatchItem* item, void const* hdr);
static void const* l4headerIpv4(uint8_t* proto, void const* data, unsigned len);
static void const* l4headerIpv6(uint8_t* proto, void const* data, unsigned len);

int matchMatches(
	struct Match* match,
	unsigned short proto,
	unsigned short _l4proto,
	void const* data, unsigned len)
{
	if (match == NULL)
		return 1;
	if (match->itemCount == 0)
		return 1;

	// Find the L4 header
	void const* hdr;
	uint8_t l4proto;
	if (proto == ETH_P_IP)
		hdr = l4headerIpv4(&l4proto, data, len);
	else
		hdr = l4headerIpv6(&l4proto, data, len);

	if (hdr == NULL)
		return 0;

	if (l4proto == IPPROTO_UDP && _l4proto == IPPROTO_SCTP) {
		/* We have an UDP encapsulated SCTP. Skip the UDP header
		 * and reset the l4proto. */
		hdr += 8;
		l4proto = IPPROTO_SCTP;
		// Re-check the boundary
		void const* endp = data + len;
		if (!IN_BOUNDS(hdr, headerlength(IPPROTO_SCTP), endp))
			return 0;
	}
	struct MatchItem* item = match->items;
	for (unsigned i = match->itemCount; i > 0; i--) {
		if (item->proto == l4proto) {
			if (!matchesItem(item, hdr))
				return 0;
		}
		item++;
	}
	return 1;
}

static int matchesItem(struct MatchItem* item, void const* hdr)
{
	/* Note that length and bounds are already checked */
	uint32_t x;
	void const* p = hdr + item->offset;
	switch (item->nbytes) {
	case 1:
		x = *((uint8_t const*)p);
		break;
	case 2:
		x = htons(*((uint16_t const*)p));
		break;
	case 4:
		x = htonl(*((uint32_t const*)p));
		break;
	default:;
	}
	if (item->mask != 0)
		x &= item->mask;
	Dx(printf("=== %u = %u\n", x, item->value));
	return x == item->value;
}

static void const* l4headerIpv4(
	uint8_t* proto, void const* data, unsigned len)
{
	void const* endp = data + len;
	struct iphdr const* h = data;
	if (!IN_BOUNDS(h, sizeof(*h), endp))
		return NULL;

	*proto = h->protocol;
	Dx(printf("L4proto = %u\n", h->protocol));
	uint32_t const* p = data;
	p += h->ihl;
	if (!IN_BOUNDS(p, headerlength(h->protocol), endp))
		return NULL;
	return p;
}

static void const* l4headerIpv6(
	uint8_t* proto, void const* data, unsigned len)
{
	void const* endp = data + len;
	struct ip6_hdr* ip6hdr = (struct ip6_hdr*)data;
	if (!IN_BOUNDS(ip6hdr, sizeof(*ip6hdr), endp))
		return NULL;

	uint8_t htype = ip6hdr->ip6_nxt;
	void const* hdr = data + sizeof(struct ip6_hdr);
	while (ipv6IsExtensionHeader(htype)) {
		if (htype == IPPROTO_FRAGMENT)
			return NULL;
		struct ip6_ext const* xh = hdr;
		if (!IN_BOUNDS(xh, sizeof(*xh), endp))
			return NULL;
		htype = xh->ip6e_nxt;
		if (xh->ip6e_len == 0)
			return NULL;			/* Corrupt header */
		hdr = hdr + (xh->ip6e_len * 8);
	}
	*proto = htype;
	Dx(printf("L4proto = %u\n", htype));
	if (!IN_BOUNDS(hdr, headerlength(htype), endp))
		return NULL;
	return hdr;
}
static char const* protostring(unsigned l4proto)
{
	switch (l4proto) {
	case IPPROTO_SCTP:
		return "sctp";
	case IPPROTO_TCP:
		return "tcp";
	case IPPROTO_UDP:
		return "udp";
	}	
	return "";
}

int matchString(struct Match* m, char* str, unsigned len)
{
	if (str == NULL || len == 0)
		return -1;
	struct MatchItem* item = m->items;
	size_t space_left = len;
	int cnt;
	*str = 0;
	for (unsigned i = m->itemCount; i > 0; i--) {
		if (space_left < len && space_left > 2) {
			// We have printed some items, add a ','
			*str++ = ',';
			space_left--;
		}
		if (item->mask != 0)
			cnt = snprintf(
				str, space_left, "%s[%u:%u]&0x%x=0x%x",
				protostring(item->proto), item->offset, item->nbytes,
				item->mask, item->value);
		else
			cnt = snprintf(
				str, space_left, "%s[%u:%u]=0x%x",
				protostring(item->proto), item->offset, item->nbytes, item->value);
		if (cnt >= space_left)
			return 1;
		space_left -= cnt;
		str += cnt;
		item++;
	}
	return 0;
}
