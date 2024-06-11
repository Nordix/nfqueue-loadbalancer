/*
   SPDX-License-Identifier: Apache-2.0
   Copyright (c) 2021-2022 Nordix Foundation
*/

#include "nfqueue.h"
#include <shmem.h>
#include <cmd.h>
#include <die.h>
#include <prime.h>
#include <fragutils.h>
#include <maglevdyn.h>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>

char const* const defaultTargetShm = "nfqlb";

static void initShm(
	char const* name, int ownFw, unsigned m, unsigned n)
{
	unsigned len = magDataDyn_len(m, n);
	struct SharedData* s = malloc(sizeof(struct SharedData) + len);
	s->ownFwmark = ownFw;
	createSharedDataOrDie(name, s, sizeof(struct SharedData) + len);
	free(s);
	s = mapSharedDataOrDie(name, O_RDWR);
	magDataDyn_init(m, n, s->mem, len);
}

static int cmdInit(int argc, char **argv)
{
	char const* shm = defaultTargetShm;
	char const* M = "997";
	char const* N = "32";
	char const* ownFw = "0";
	struct Option options[] = {
		{"help", NULL, 0,
		 "init [options]\n"
		 "  Initiate shared mem structures"},
		{"ownfw", &ownFw, 0, "Own FW mark. Only used in the LB-tier!"},
		{"shm", &shm, 0, "Target shared memory"},
		{"N", &N, 0, "Maglev max targets"},
		{"M", &M, 0, "Maglev lookup table size"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);
	unsigned m, n, p;
	m = atoi(M);
	if (m < 3)
		p = 3;
	else
		p = primeBelow(m);
	if (p != m) {
		printf("M adjusted; %u -> %u\n", m, p);
		m = p;
	}
	n = atoi(N);
	if (n > m)
		die("N can't be larger than M\n");
	initShm(shm, atoi(ownFw), m, n);

	return 0;
}
static int cmdDelete(int argc, char **argv)
{
	char const* shm = NULL;
	struct Option options[] = {
		{"help", NULL, 0,
		 "delete [options]\n"
		 "  Delete shared mem structures"},
		{"shm", &shm, 1, "Target shared memory"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);
	if (shm_unlink(shm) != 0 && errno != ENOENT)
		die("%s\n", strerror(errno));
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
	s = mapSharedDataOrDie(shm, O_RDONLY);
	if (s == NULL)
		die("Failed to open shared mem; %s\n", shm);
	printf("Shm: %s\n", shm);
	printf("  Fw: own=%d\n", s->ownFwmark);

	struct MagDataDyn magd;
	magDataDyn_map(&magd, s->mem);
	printf("  Maglev: M=%d, N=%d\n", magd.M, magd.N);
	printf("   Lookup:");
	for (int i = 0; i < 25 && i < magd.M; i++)
		printf(" %d", magd.lookup[i]);
	printf("...\n");
	printf("   Active:");
	for (int i = 0; i < magd.N; i++) {
		if (magd.active[i] >= 0)
			printf(" %d(%d)", magd.active[i], i);
	}
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
	struct fragStats* sft = mapSharedDataOrDie(ftShm, O_RDONLY);
	fragPrintStats(sft);
	return 0;
}

__attribute__ ((__constructor__)) static void addCommands(void) {
	addCmd("init", cmdInit);
	addCmd("delete", cmdDelete);
	addCmd("show", cmdShow);
	addCmd("stats", cmdStats);
	addCmd("primebelow", cmdPrimeBelow);
}
