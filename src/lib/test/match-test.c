/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021-2022 Nordix Foundation
*/

#include <match.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

// Pcap help functions
#include "pcap.c"

static int cmp(char const* a, char const* b);
static unsigned matchCount(struct Match* m, unsigned l4proto);

int main(int argc, char* argv[])
{
	struct Match* m;
	char const* err;

	// Validation tests
	err = matchValidate("Totally wrong!");
	assert(err != NULL);
	err = matchValidate("sctp[3:4] & 0xffffffff = 44");
	assert(err == NULL);
	err = matchValidate("sctp[4:2] = 0xa44");
	assert(err == NULL);
	err = matchValidate("sctp[4:3] = 0");
	assert(err != NULL);
	// Just within bounds test
	err = matchValidate("udp[6:2] = 0");
	assert(err == NULL);
	err = matchValidate("sctp[10:2] = 0");
	assert(err == NULL);
	err = matchValidate("tcp[18:2] = 0");
	assert(err == NULL);
	// Out of bounds tests
	err = matchValidate("udp[6:4] = 0");
	assert(err != NULL);
	err = matchValidate("sctp[10:4] = 0");
	assert(err != NULL);
	err = matchValidate("tcp[18:4] = 0");
	assert(err != NULL);

	// Match tests
	matchDestroy(NULL);
	m = matchCreate();
	assert(m != 0);
	assert(matchItemCount(NULL) == 0);
	assert(matchItemCount(m) == 0);
	matchDestroy(m);			/* <- destroy */

	m = matchCreate();
	err = matchAdd(m, "sctp[3:4] & 0xffffffff = 44");
	assert(err == NULL);
	assert(matchItemCount(m) == 1);

	err = matchAdd(m, "sctp[4:2] = 0xa44");
	assert(err == NULL);
	assert(matchItemCount(m) == 2);

	err = matchAdd(m, "sctp[4:3] = 0");
	assert(err != NULL);
	assert(matchItemCount(m) == 2);

	// Just within bounds test
	err = matchAdd(m, "udp[6:2] = 0");
	assert(err == NULL);
	err = matchAdd(m, "sctp[10:2] = 0");
	assert(err == NULL);
	err = matchAdd(m, "tcp[18:2] = 0");
	assert(err == NULL);
	assert(matchItemCount(m) == 5);

	// Out of bounds tests
	err = matchAdd(m, "udp[6:4] = 0");
	assert(err != NULL);
	err = matchAdd(m, "sctp[10:4] = 0");
	assert(err != NULL);
	err = matchAdd(m, "tcp[18:4] = 0");
	assert(err != NULL);

	assert(matchItemCount(m) == 5);
	matchDestroy(m);			/* <- destroy */

	// matchPrint tests
	char buf[256];
	m = matchCreate();
	buf[0] = 'A';
	assert(matchString(m, buf, 256) == 0);
	assert(buf[0] == 0);
	err = matchAdd(m, "sctp[3:4] & 0xffffffff = 33");
	assert(err == NULL);
	memset(buf, 0, sizeof(buf));
	assert(matchString(m, buf, 256) == 0);
	assert(cmp(buf, "sctp[3:4]&0xffffffff=0x21") == 0);
	// Buffer size test
	memset(buf, 0, sizeof(buf));
	assert(matchString(m, buf, 26) == 0);
	assert(cmp(buf, "sctp[3:4]&0xffffffff=0x21") == 0);
	memset(buf, 0, sizeof(buf));
	assert(matchString(m, buf, 25) == 1);
	assert(cmp(buf, "sctp[3:4]&0xffffffff=0x2") == 0);
	// More than one item
	err = matchAdd(m, "sctp[4:2] = 0xa44");
	assert(err == NULL);
	memset(buf, 0, sizeof(buf));
	assert(matchString(m, buf, 256) == 0);
	assert(cmp(buf, "sctp[3:4]&0xffffffff=0x21,sctp[4:2]=0xa44") == 0);
	matchDestroy(m);			/* <- destroy */


	// Pcap tests
	shuffle(NULL, 0);			/* (suspress compiler warning) */
	readPcapData("lib/test/telnet-ipv4.pcap");

	m = matchCreate();
	assert(matchCount(m, 0) == 20);/* All packets match an empty set */
	assert(matchAdd(m, "tcp[2:2] = 23") == NULL);
	assert(matchCount(m, 0) == 10);
	matchDestroy(m);			/* <- destroy */

	m = matchCreate();
	assert(matchAdd(m, "tcp[12:4] & 0x00020000 = 0x00020000") == NULL);
	assert(matchCount(m, 0) == 2);	/* SYN + SYN/ACK */
	matchDestroy(m);			/* <- destroy */

	readPcapData("lib/test/telnet-ipv6.pcap");

	m = matchCreate();
	assert(matchAdd(m, "tcp[0:4] & 0xffff = 23") == NULL);
	assert(matchCount(m, 0) == 9);
	matchDestroy(m);			/* <- destroy */

	m = matchCreate();
	assert(matchAdd(m, "tcp[2:1] = 0") == NULL);
	assert(matchAdd(m, "tcp[3:1] = 23") == NULL);
	assert(matchCount(m, 0) == 9);
	matchDestroy(m);			/* <- destroy */

	readPcapData("lib/test/sctp-encap-ipv4.pcap");
	m = matchCreate();
	assert(matchAdd(m, "udp[2:2]=6000") == NULL);
	assert(matchCount(m, 0) == 0);
	matchDestroy(m);			/* <- destroy */
	m = matchCreate();
	assert(matchAdd(m, "sctp[2:2]=6000") == NULL);
	assert(matchCount(m, IPPROTO_SCTP) == 6);
	assert(matchCount(m, 0) == 11);
	matchDestroy(m);			/* <- destroy */

	printf("=== match-test OK\n");
	return 0;
}

static unsigned matchCount(struct Match* m, unsigned l4proto)
{
	unsigned cnt = 0;
	for (unsigned i = 0; i < nPackets; i++) {
		if (matchMatches(m, packets[i].protocol, l4proto, packets[i].data, packets[i].len))
			cnt++;
	}
	//printf("matchCount: %u\n", cnt);
	return cnt;
}

static int cmp(char const* a, char const* b)
{
	if (strcmp(a, b) != 0) {
		printf("=== [%s] != [%s]\n", a, b);
		return 1;
	}
	return 0;
}
