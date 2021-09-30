/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

#include "die.h"
#include "conntrack.h"
#include "fragutils.h"
#include "reassembler.h"
#include <cmd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <pcap/pcap.h>
#include <netinet/ether.h>
#include <stdlib.h>
#include <assert.h>

// Debug macros
#define Dx(x) x
#define D(x)

struct Packet {
	unsigned protocol;
	unsigned len;
	void* data;
};
#define MAX_PACKETS 4096
static unsigned nPackets = 0;
static struct Packet packets[MAX_PACKETS];

static void storeHandler(
	u_char *user, const struct pcap_pkthdr *h, const u_char *bytes)
{
	if (nPackets >= MAX_PACKETS)
		die("MAX_PACKETS reached\n");

	struct Packet* p = packets + nPackets++;
	struct ethhdr* eh = (struct ethhdr*)bytes;
	p->protocol = ntohs(eh->h_proto);
	p->len = h->len - sizeof(struct ethhdr);
	p->data = malloc(p->len);
	memcpy(p->data, bytes + sizeof(struct ethhdr), p->len);
}

static void readPcapData(char const* file)
{
    char errbuf[PCAP_ERRBUF_SIZE];
	pcap_t* fp = pcap_open_offline(file, errbuf);
    if (fp == NULL)
		die("pcap_open_offline() failed: %s\n", errbuf);	
	if (pcap_loop(fp, 0, storeHandler, NULL) < 0)
        die("pcap_loop() failed: %s\n", pcap_geterr(fp));
}

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
	struct Option options[] = {
		{"help", NULL, 0,
		 "parse [file]\n"
		 "  Read pcap file and parse fragments"},
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

	struct FragTable* ft = fragTableCreate(109, 100, 1000, 1500, 200);
	fragRegisterFragReassembler(ft, createReassembler(200));

	int rc = 0;
	unsigned hash;
	struct timespec now = {0};
	for (unsigned i = 0; i < nPackets; i++) {
		struct Packet* p = packets + i;
		switch (p->protocol) {
		case ETH_P_IP:
			rc = ipv4Fragment(ft, &now, NULL, p->data, p->len, &hash);
			break;
		case ETH_P_IPV6:
			rc = ipv6Fragment(ft, &now, NULL, p->data, p->len, &hash);
			break;
		default:;
		}
		assert(rc == 0);
	}

	struct fragStats stats;
	fragGetStats(ft, &now, &stats);
	fragPrintStats(&stats);

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

