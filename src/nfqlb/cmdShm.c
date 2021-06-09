/*
   SPDX-License-Identifier: Apache-2.0
   Copyright (c) 2021 Nordix Foundation
*/

#include "nfqueue.h"
#include <shmem.h>
#include <cmd.h>
#include <die.h>
#include <prime.h>
#include <fragutils.h>
#include <stdlib.h>
#include <stdio.h>

char const* const defaultTargetShm = "nfqlb";

static void initShm(
	char const* name, int ownFw, int fwOffset, unsigned m, unsigned n)
{
	struct SharedData s;
	s.ownFwmark = ownFw;
	s.fwOffset = fwOffset;
	initMagData(&s.magd, m, n);
	populate(&s.magd);
	createSharedDataOrDie(name, &s, sizeof(s));
}

static int cmdInit(int argc, char **argv)
{
	char const* shm = defaultTargetShm;
	char const* M = "997";
	char const* N = "32";
	char const* offset = "100";
	char const* ownFw = NULL;
	struct Option options[] = {
		{"help", NULL, 0,
		 "init [options]\n"
		 "  Initiate shared mem structures"},
		{"ownfw", &ownFw, REQUIRED, "Own FW mark (not offset adjusted)"},
		{"shm", &shm, 0, "Target shared memory"},
		{"offset", &offset, 0, "FW offset"},
		{"N", &N, 0, "Maglev max targets"},
		{"M", &M, 0, "Maglev lookup table size"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);
	unsigned m, n, p;
	m = atoi(M);
	p = primeBelow(m > MAX_M ? MAX_M : m);
	if (p != m) {
		printf("M adjusted; %u -> %u\n", m, p);
		m = p;
	}
	n = atoi(N);
	if (n > MAX_N)
		n = MAX_N;
	initShm(
		shm, atoi(ownFw), atoi(offset), m, n);

	return 0;
}

static int cmdPrimeBelow(int argc, char **argv)
{
	struct Option options[] = {
		{"help", NULL, 0,
		 "primebelow <n>\n"
		 "  Give a prime below the passed number"},
		{0, 0, 0, 0}
	};
	int nopt = parseOptionsOrDie(argc, argv, options);
	argc -= nopt;
	argv += nopt;
	if (argc > 0)
		printf("%u\n", primeBelow(atoi(*argv)));
	return 0;
}

static int cmdShow(int argc, char **argv)
{
	char const* shm = defaultTargetShm;
	struct Option options[] = {
		{"help", NULL, 0,
		 "show [options]\n"
		 "  Show shared mem structures"},
		{"shm", &shm, 0, "Shared memory"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);
	struct SharedData* s;
	s = mapSharedDataOrDie(shm, sizeof(*s), O_RDONLY);
	printf("Shm: %s\n", shm);
	printf("  Fw: own=%d, offset=%d\n", s->ownFwmark, s->fwOffset);
	printf("  Maglev: M=%d, N=%d\n", s->magd.M, s->magd.N);
	printf("   Lookup:");
	for (int i = 0; i < 25; i++)
		printf(" %d", s->magd.lookup[i]);
	printf("...\n");
	printf("   Active:");
	for (int i = 0; i < s->magd.N; i++)
		printf(" %u", s->magd.active[i]);
	printf("\n");
	return 0;
}

static int cmdStats(int argc, char **argv)
{
	char const* ftShm = "ftshm";
	struct Option options[] = {
		{"help", NULL, 0,
		 "stats [options]\n"
		 "  Show frag table stats"},
		{"ft_shm", &ftShm, 0, "Frag table; shared memory stats"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);
	struct fragStats* sft = mapSharedDataOrDie(ftShm, sizeof(*sft), O_RDONLY);
	fragPrintStats(sft);
	return 0;
}

__attribute__ ((__constructor__)) static void addCommands(void) {
	addCmd("init", cmdInit);
	addCmd("show", cmdShow);
	addCmd("stats", cmdStats);
	addCmd("primebelow", cmdPrimeBelow);
}
