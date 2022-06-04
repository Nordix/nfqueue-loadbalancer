/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021-2022 Nordix Foundation
*/
// This file should be included
#include <die.h>
#include <pcap/pcap.h>
#include <netinet/ether.h>
#include <stdlib.h>

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
	nPackets = 0;
	pcap_t* fp = pcap_open_offline(file, errbuf);
    if (fp == NULL)
		die("pcap_open_offline() failed: %s\n", errbuf);	
	if (pcap_loop(fp, 0, storeHandler, NULL) < 0)
        die("pcap_loop() failed: %s\n", pcap_geterr(fp));
	pcap_close(fp);
}

static void shuffle(struct Packet* packets, unsigned cnt)
{
	if (packets == NULL || cnt == 0)
		return;
	for (unsigned shuff = 0; shuff < (cnt * 5); shuff++) {
		unsigned i1 = rand() % cnt;
		unsigned i2 = rand() % cnt;
		struct Packet tmp = packets[i1];
		packets[i1] = packets[i2];
		packets[i2] = tmp;
	}
}

static void freePackets(struct Packet* packets, unsigned cnt)
{
	if (packets == NULL || cnt == 0)
		return;
	for (int i = 0; i < cnt; i++)
		free(packets[i].data);
}
