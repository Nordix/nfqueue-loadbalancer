#pragma once
/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021-2022 Nordix Foundation
*/

#include <netinet/in.h>

struct Match;

/*
  The traditional create/destroy functions
 */
struct Match* matchCreate(void);
void matchDestroy(struct Match* match);

/*
  Add a match statement in the form (same as for "tcpdump");

    "proto[x,y] & z = V"

  proto=tcp|udp|sctp, x=byte-offset, y=nbytes, z=mask (optional)
  y may only be 1,2,4

  Regexp;
  "^(sctp|tcp|udp)\[[0-9]+ *: *[124]\]( *& *0x[0-9a-f]+)? *= *([0-9]+|0x[0-9a-f]+)$"

  Multiple match statement are AND'ed.

  Returns same as matchValidate()
 */
char const* matchAdd(struct Match* match, char const* str);

/*
  Returns;
  NULL - OK
  != NULL - An error string. The string is statically allocated and will
            be overwritten on subsequent calls.
 */
char const* matchValidate(char const* str);

/*
  Returns the number of items
 */
unsigned matchItemCount(struct Match* match);

/*
  Check if the l4 header in the passed packet matches all statement
  that has the same l4proto. Statement that has another l4proto are ignored, so
  for instance if there are no statement at all, all packets match.
  Return !=0 if the passed packet matches.

  UDP encapsulated SCTP;

  If the "l4proto" option is set to IPPROTO_SCTP and an UDP packet is
  received then an UDP encapsulated SCTP is assumed. The UDP header is
  skipped and the match is performed on the SCTP header.
 */
int matchMatches(
	struct Match* match,
	unsigned short proto,			 /* ETH_P_IP | ETH_P_IPV6 */
	unsigned short l4proto,			 /* Set to IPPROTO_SCTP for UDP encap */
	void const* data, unsigned len); /* The IP packet (not the L4 header) */

/*
  Prints matches as comma-separated values.
  Returns;
   0 - Ok
   1 - Can't fit in the passed string
  -1 - Fail
 */
int matchString(struct Match* m, char* str, unsigned len);
