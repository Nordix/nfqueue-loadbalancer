/*
   SPDX-License-Identifier: Apache-2.0
   Copyright (c) 2021 Nordix Foundation
*/

#include "nfqueue.h"
#include <shmem.h>
#include <cmd.h>
#include <fragutils.h>
#include <stdlib.h>
#include <stdio.h>

char const* const defaultTargetShm = "nfqlb";

static void maglevInit(struct MagData* m)
{
	initMagData(m, 997, 32);
	populate(m);
}

static void initShm(char const* name, int ownFw, int fwOffset)
{
	struct SharedData s;
	s.ownFwmark = ownFw;
	s.fwOffset = fwOffset;
	maglevInit(&s.magd);
	createSharedDataOrDie(name, &s, sizeof(s));
}

static int cmdInit(int argc, char **argv)
{
	char const* targetShm = defaultTargetShm;
	char const* lbShm = NULL;
	char const* targetOffset = NULL;
	char const* lbOffset = NULL;
	char const* ownFw = NULL;
	struct Option options[] = {
		{"help", NULL, 0,
		 "init [options]\n"
		 "  Initiate shared mem structures"},
		{"ownfw", &ownFw, REQUIRED, "Own FW mark (not offset adjisted)"},
		{"tshm", &targetShm, 0, "Target shared memory"},
		{"toffset", &targetOffset, 0, "Target FW offset"},
		{"lbshm", &lbShm, 0, "Lb shared memory"},
		{"lboffset", &lbOffset, 0, "Lb FW offset"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);
	initShm(
		targetShm, atoi(ownFw), targetOffset == NULL ? 100:atoi(targetOffset));
	if (lbShm != NULL)
		initShm(
			lbShm, atoi(ownFw), lbOffset == NULL ? 200:atoi(targetOffset));
	return 0;
}

static void showShm(char const* name)
{
	struct SharedData* s;
	s = mapSharedDataOrDie(name, sizeof(*s), O_RDONLY);
	printf("Shm: %s\n", name);
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
}

static int cmdShow(int argc, char **argv)
{
	char const* targetShm = defaultTargetShm;
	char const* lbShm = NULL;
	struct Option options[] = {
		{"help", NULL, 0,
		 "show [options]\n"
		 "  Show shared mem structures"},
		{"tshm", &targetShm, 0, "Target shared memory"},
		{"lbshm", &lbShm, 0, "Lb shared memory"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);
	if (lbShm != NULL)
		showShm(lbShm);
	showShm(targetShm);
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
}
