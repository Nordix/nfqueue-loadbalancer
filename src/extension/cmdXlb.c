/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

#include <nfqueue.h>
#include <iputils.h>
#include <shmem.h>
#include <cmd.h>
#include <die.h>
#include <maglevdyn.h>

#include <stdlib.h>
#include <stdio.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/ether.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <pthread.h>

static struct SharedData* st;
static struct MagDataDyn magd;

#ifdef VERBOSE
#define D(x)
#define Dx(x) x
#else
#define D(x)
#define Dx(x)
#endif

#define FW(table) table.active[table.lookup[hash % table.M]]


static int packetHandleFn(
	unsigned short proto, void* data, unsigned len)
{
	struct ctKey key;
	uint64_t fragid;
	int rc = getHashKey(&key, 0, &fragid, proto, data, len);
	if (rc < 0)
		return -1;
	unsigned hash = hashKeyAddresses(&key);
	Dx(printf("Packet; len=%u, fw=%d\n", len, FW(magd)));
	return FW(magd);
}

static void *packetHandleThread(void* Q)
{
	nfqueueRun((intptr_t)Q);
	return NULL;
}

static int cmdXlb(int argc, char **argv)
{
	char const* targetShm = defaultTargetShm;
	char const* qnum = "2";
	char const* qlen = "1024";
	struct Option options[] = {
		{"help", NULL, 0,
		 "xlb [options]\n"
		 "  Load-balance on addresses only"},
		{"tshm", &targetShm, 0, "Target shared memory"},
		{"queue", &qnum, 0, "NF-queues to listen to (default 2)"},
		{"qlength", &qlen, 0, "Lenght of queues (default 1024)"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);
	st = mapSharedDataOrDie(targetShm, O_RDONLY);
	magDataDyn_map(&magd, st->mem);


	nfqueueInit(packetHandleFn, atoi(qlen), 576);

	/*
	  The qnum may be a range like "0:3" in which case we go
	  multi-threading and listen to all queues in the range;
	 */
	unsigned Q = atoi(qnum);
	if (strchr(qnum, ':') != NULL) {
		unsigned first, last;
		if (sscanf(qnum, "%u:%u", &first, &last) != 2)
			die("queue invalid [%s]\n", qnum);
		if (first > last)
			die("queue invalid [%s]\n", qnum);
		for (Q = first; Q < last; Q++) {
			pthread_t tid;
			if (pthread_create(&tid, NULL, packetHandleThread, (void*)(intptr_t)Q) != 0)
				die("Failed pthread_create for Q=%u\n", Q);
		}
		Q = last;				/* Last will go to the main thread */
	}
	return nfqueueRun(Q);
}

__attribute__ ((__constructor__)) static void addCommands(void) {
	addCmd("xlb", cmdXlb);
}
