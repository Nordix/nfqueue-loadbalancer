/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021-2022 Nordix Foundation
*/

#include <flow.h>
#include <argv.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdlib.h>

#define D(x)
#define Dx(x) x

int main(int argc, char* argv[])
{
	struct FlowSet* f;
	char const* err;
	char name[32];
	char port[32];
	struct ctKey key;

	// Basic
	flowSetDelete(NULL);
	f = flowSetCreate(NULL);
	assert(f != NULL);
	assert(flowSetSize(f) == 0);
	flowSetDelete(f);

	// Add and delete
	f = flowSetCreate(NULL);
	err = flowDefine(f, "f100", 100, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0);
	assert(err == NULL);
	err = flowDefine(f, "f200", 200, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0);
	assert(err == NULL);
	err = flowDefine(f, "f300", 300, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0);
	assert(err == NULL);
	assert(flowSetSize(f) == 3);
	flowDelete(f, "f500", NULL);		/* no-op */
	assert(flowSetSize(f) == 3);
	flowDelete(f, "f200", NULL);
	assert(flowSetSize(f) == 2);
	flowDelete(f, "f100", NULL);
	assert(flowSetSize(f) == 1);
	err = flowDefine(f, "f200", 200, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0);
	assert(flowSetSize(f) == 2);
	flowSetDelete(f);

	// Simple priority
	extern int flowSetIsSorted(struct FlowSet* set);
	f = flowSetCreate(NULL);
	void* user_ref = (void*)500;
	for (unsigned int i = 100; i > 0; i--) {
		sprintf(name, "flow%u", i);
		sprintf(port, "%u", i + 20);
		err = flowDefine(
			f, name, i, user_ref++, NULL, port, NULL, NULL, NULL, NULL, 0);
		assert(err == NULL);
	}
	assert(flowSetSize(f) == 100);
	assert(flowSetIsSorted(f));
	for (unsigned int i = 100; i > 0; i--) {
		sprintf(name, "flow%u", i);
		sprintf(port, "%u", i + 20);
		err = flowDefine(
			f, name, 101-i, user_ref, NULL, port, NULL, NULL, NULL, NULL, 0);
		assert(flowLookupName(f, name, NULL) == user_ref);
		user_ref++;
		assert(err == NULL);
	}
	assert(flowSetSize(f) == 100);
	assert(flowSetIsSorted(f));
	flowSetDelete(f);

	// Re-prioritize
	f = flowSetCreate(NULL);
	err = flowDefine(f, "p200", 200, (void*)1, NULL, NULL, NULL, NULL, NULL, NULL, 0);
	assert(err == NULL);
	err = flowDefine(f, "pX", 100, (void*)2, NULL, NULL, NULL, NULL, NULL, NULL, 0);
	assert(err == NULL);
	assert(flowSetSize(f) == 2);
	memset(&key, 0, sizeof(key));
	assert(flowLookup(f, &key, 0,NULL,0, NULL) == (void*)1);
	err = flowDefine(f, "pX", 400, (void*)2, NULL, NULL, NULL, NULL, NULL, NULL, 0);
	assert(err == NULL);
	assert(flowSetSize(f) == 2);
	assert(flowLookup(f, &key, 0,NULL,0,  NULL) == (void*)2);
	flowSetDelete(f);

	// Protocols only
	f = flowSetCreate(NULL);
	char const* ptcp[] = { "tcp", NULL };
	err = flowDefine(f, "tcp100", 100, (void*)1, ptcp, NULL,NULL,NULL,NULL,NULL, 0);
	assert(err == NULL);
	char const* pudp[] = { "udp", NULL };
	err = flowDefine(f, "udp100", 100, (void*)1, pudp, NULL,NULL,NULL,NULL,NULL, 0);
	assert(err == NULL);
	memset(&key, 0, sizeof(key));
	key.ports.proto = IPPROTO_SCTP;
	assert(flowLookup(f, &key, 0,NULL,0,  NULL) == NULL);
	char const* psctp[] = { "sctp", NULL };
	err = flowDefine(f, "sctp100", 100, (void*)1, psctp, NULL,NULL,NULL,NULL,NULL, 0);
	assert(err == NULL);
	assert(flowLookup(f, &key, 0,NULL,0,  NULL) == (void*)1);
	char const* ptipc[] = { "tipc", NULL };
	err = flowDefine(f, "tipc100", 100, (void*)1, ptipc, NULL,NULL,NULL,NULL,NULL, 0);
	assert(err != NULL);
	char const* pall[] = { "TCP", "udP", "ScTp", NULL };
	err = flowDefine(f, "all10", 200, (void*)2, pall, NULL,NULL,NULL,NULL, NULL, 0);
	assert(err == NULL);
	assert(flowLookup(f, &key, 0,NULL,0,  NULL) == (void*)2);
	flowSetDelete(f);

	// Basic address
	f = flowSetCreate(NULL);
	char const* adr01[] = {"1111:2222::5555/64", "10.10.10.10/16", NULL};
	err = flowDefine(f, "adr01", 1, (void*)2, NULL, NULL, NULL, adr01, NULL, NULL, 0);
	assert(err == NULL);
	memset(&key, 0, sizeof(key));
	D(flowSetPrint(stdout, f, NULL));
	assert(flowLookup(f, &key, 0,NULL,0,  NULL) == NULL);
	assert(inet_pton(AF_INET6, "::ffff:10.10.222.4", &key.dst) == 1);
	assert(flowLookup(f, &key, 0,NULL,0,  NULL) == (void*)2);
	assert(inet_pton(AF_INET6, "1111:2222:0:0:ffff::", &key.dst) == 1);
	assert(flowLookup(f, &key, 0,NULL,0,  NULL) == (void*)2);
	flowSetDelete(f);

	// Basic port
	f = flowSetCreate(NULL);
	char const* ports = "22-30, 1025, 20000-30000, 22000-23000, 25, 27";
	err = flowDefine(f, "adr01", 1, (void*)2, NULL, ports, NULL, NULL, NULL,NULL, 0);
	assert(err == NULL);
	memset(&key, 0, sizeof(key));
	assert(flowLookup(f, &key, 0,NULL,0,  NULL) == NULL);
	key.ports.dst = htons(23);
	assert(flowLookup(f, &key, 0,NULL,0, NULL) == (void*)2);
	err = flowDefine(f, "adr01", 1, (void*)2, NULL, ports, ports, NULL, NULL,NULL, 0);
	assert(err == NULL);
	assert(flowSetSize(f) == 1);
	D(flowSetPrint(stdout, f, NULL));
	assert(flowLookup(f, &key, 0,NULL,0, NULL) == NULL);
	key.ports.src = htons(22000);
	assert(flowLookup(f, &key, 0,NULL,0, NULL) == (void*)2);
	flowSetDelete(f);

	// Udpencap
	f = flowSetCreate(NULL);
	err = flowDefine(f, "u01", 1, (void*)2, NULL,NULL,NULL,NULL,NULL,NULL, 1234);
	unsigned short udpencap = 0;
	memset(&key, 0, sizeof(key));
	assert(flowLookup(f, &key, 0,NULL,0, &udpencap) == (void*)2);
	assert(udpencap == 1234);
	udpencap = 0;
	assert(flowDelete(f, "u01", &udpencap) == (void*)2);
	assert(udpencap == 1234);
	flowSetDelete(f);

	// Delete flows (several segv's bugs in this area!)
	f = flowSetCreate(NULL);
	err = flowDefine(f, "adr01", 1, (void*)1, NULL, ports, NULL, NULL, NULL,NULL, 0);
	err = flowDefine(f, "adr02", 5, (void*)5, NULL, "10", NULL, NULL, NULL,NULL, 0);
	memset(&key, 0, sizeof(key));
	key.ports.dst = htons(10);
	assert(flowLookup(f, &key, 0,NULL,0, NULL) == (void*)5);
	key.ports.dst = htons(30);
	assert(flowLookup(f, &key, 0,NULL,0, NULL) == (void*)1);
	assert(flowDelete(f, "adr01", NULL) == (void*)1);
	assert(flowLookup(f, &key, 0,NULL,0, NULL) == NULL);
	assert(flowDelete(f, "adr02", NULL) == (void*)5);
	key.ports.dst = htons(10);
	assert(flowLookup(f, &key, 0,NULL,0, NULL) == NULL);
	flowSetDelete(f);

	// Flow update test
	extern int flowSetEqual(struct FlowSet* f1, struct FlowSet* f2);
	struct FlowSet* f1 = flowSetCreate(NULL);
	struct FlowSet* f2 = flowSetCreate(NULL);
	assert(flowSetEqual(f1, f2));
	char const** match = mkargv("sctp[2:2]=6000, tcp[2:2]=23", ",");
	char const** adrs = mkargv("10.0.0.0/32,fd00::10.0.0.0/128", ",");
	err = flowDefine(
		f1, "F1", 100, NULL, psctp, "6000-6010", "7000,44", adrs, adrs,
		match, 9899);
	assert(err == NULL);
	assert(!flowSetEqual(f1, f2));
	err = flowDefine(
		f2, "F1", 100, NULL, psctp, "6000-6010", "7000,44", adrs, adrs,
		match, 9899);
	assert(err == NULL);
	assert(flowSetEqual(f1, f2));
	// Re-define
	err = flowDefine(
		f2, "F1", 100, NULL, psctp, "6000-6010", "7000,44", adrs, adrs,
		match, 9899);
	assert(flowSetEqual(f1, f2));

	flowSetDelete(f1);
	flowSetDelete(f2);
	free(match);
	free(adrs);

	printf("=== flow-test OK\n");
	return 0;
}
