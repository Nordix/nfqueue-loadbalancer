#pragma once
/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021-2022 Nordix Foundation
*/

#include <conntrack.h>

#define IN_BOUNDS(p,o,e) (((void const*)p + o) < (void const*)(e))

int ipv6IsExtensionHeader(unsigned htype);

/*
  Get the key used for hashing.
  udpencap - UDP encapsulated SCTP. IN HOST BYTE ORDER!!!!
  Returns;
  <0 - Failed
  0  - Normal packet. Ports are valid
  1  - First fragment. Ports are valid, *fragid is returned
  2  - Sub-sequent fragment. Id is valid
  4  - UDP packet, dport matching the "udpEncap" port. The proto is set
       to sctp and the ports are taken from the (inner) sctp header
  8  - ICMP packet with inner header (e.g packet too big). The addresses
       and ports are taken from the inner packet and reversed.
  16 - ICMP without inner header, e.g "ping". Id may be valid.
  32 - Only addresses are valid
 */
int getHashKey(
	struct ctKey* key, unsigned short udpencap, uint64_t* fragid,
	unsigned proto, void const* data, unsigned len);

// Hash on addresses only
unsigned hashKeyAddresses(struct ctKey* key);

// The default hash function.
// Hashes on the entire key, unless proto=sctp in which case we hash on
// ports only.
unsigned hashKey(struct ctKey* key);

/*
  parseAddress parses an address into a struct sockaddr_storage.
  Example addresses;
    "tcp:0.0.0.0:4567"
    "tcp:[::1]:4567"
    "unix:nfqlb"
  Returns; 0 if OK
 */
int parseAddress(
	char const* adr, struct sockaddr_storage* sas, socklen_t* len);

// Return an L4 protocol as a string.
// The passed buf is used for unknown protocols. If NULL -> not thread-safe.
char const* protostr(unsigned short p, char* buf);

// Parse a protocol string. Valid values "tcp|udp|sctp" (case insensitive)
// Returns the protocol number or 0 (IPPROTO_IP) if parsing failed.
unsigned parseProto(char const* str);

/*
  Print ICMP info using the passed "outf()" function.
 */
void printIcmp(
	int (*outf)(const char *fmt, ...),
	unsigned proto, void const* data, unsigned len);
