/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021-2022 Nordix Foundation
*/

#include "conntrack.h"
#include "iputils.h"
#include "fragutils.h"
#include "reassembler.h"
#include <cmd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <time.h>

// Debug macros
#define Dx(x) x
#define D(x)

// Pcap help functions
#include "pcap.c"


static int
cmdRead(int argc, char* argv[])
{
	struct Option options[] = {
		{"help", NULL, 0,
		 "read [file]\n"
		 "  Read and check a pcap file (or stdin)"},
		{0, 0, 0, 0}
	};
	int nopt = parseOptionsOrDie(argc, argv, options);
	argc -= nopt;
	argv += nopt;

	char const* file = "-";
	if (argc > 0) {
		file = argv[0];
	}

	readPcapData(file);
	for (unsigned i = 0; i < nPackets; i++) {
		char const* pstr = "?   ";
		switch (packets[i].protocol) {
		case ETH_P_IP: pstr = "IPv4"; break;
		case ETH_P_IPV6: pstr = "IPv6"; break;
		default:;
		}
		printf("%4u %s %u\n", i, pstr, packets[i].len);
	}

	return 0;
}

static int
cmdParse(int argc, char* argv[])
{
	char const* shuffleStr = "no";
	char const* quietStr = "no";
	char const* file = "";
	struct Option options[] = {
		{"help", NULL, 0,
		 "parse [file|-]\n"
		 "  Read pcap file and parse fragments"},
		{"file", &file, REQUIRED, "Pcap file. '-' = stdin"},
		{"shuffle", &shuffleStr, 0, "Shuffle the packets"},
		{"quiet", &quietStr, 0, "Suspress stats printout"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);

	srand(time(NULL));

	readPcapData(file);
	if (shuffleStr == NULL)
		shuffle(packets, nPackets);

	struct FragTable* ft = fragTableCreate(109, 100, 1000, 1500, 200);
	fragRegisterFragReassembler(ft, createReassembler(200));

	int rc = 0;
	int hash;
	struct timespec now = {0};
	for (unsigned i = 0; i < nPackets; i++) {
		struct Packet* p = packets + i;
		struct ctKey key;
		uint64_t fragid;
		rc = getHashKey(&key, 0, &fragid, p->protocol, p->data, p->len, 1);
		if (rc & 1) {
			hash = hashKey(&key, 1);
			key.id = fragid;
			rc = handleFirstFragment(ft, &now, &key, hash, p->data, p->len);
		} else if (rc & 2) {
			rc = fragGetValueOrStore(ft, &now, &key, &hash, p->data, p->len);
		} else {
			rc = 0;
		}
		if (rc < 0) {
			struct fragStats stats;
			fragGetStats(ft, &now, &stats);
			fragPrintStats(&stats);
			assert(0);
		}
	}

	if (quietStr != NULL) {
		struct fragStats stats;
		fragGetStats(ft, &now, &stats);
		fragPrintStats(&stats);
	} else {
		printf("==== pcap-test OK [%s]\n", file);
	}

	fragTableDestroy(ft);
	freePackets(packets, nPackets);
	return 0;
}

int main(int argc, char *argv[])
{
	// Make logs to stdout/stderr appear when output is redirected
	setlinebuf(stdout);
	setlinebuf(stderr);

	if (argc < 2) {
		printf("==== pcap-test, nothing tested\n");
		return 0;
	}

	addCmd("read", cmdRead);
	addCmd("parse", cmdParse);
	return handleCmd(argc, argv);
}

