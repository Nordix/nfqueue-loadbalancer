/*
   SPDX-License-Identifier: Apache-2.0
   Copyright (c) 2021 Nordix Foundation
*/

#include "nfqueue.h"
#include <shmem.h>
#include <cmd.h>
#include <die.h>
#include <fragutils.h>
#include <stdlib.h>
#include <stdio.h>

char const* const defaultTargetShm = "nfqlb";

static int isPrime(unsigned n);
static unsigned primeBelow(unsigned n);

static void initShm(
	char const* name, int ownFw, int fwOffset, unsigned m, unsigned n)
{
	if (!isPrime(m))
		die("M is not a prime; %u\n", m);
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

//static unsigned const nprimes = 167;
static unsigned const primes[] = {
  3,
  5,
  7,
  11,
  13,
  17,
  19,
  23,
  29,
  31,
  37,
  41,
  43,
  47,
  53,
  59,
  61,
  67,
  71,
  73,
  79,
  83,
  89,
  97,
  101,
  103,
  107,
  109,
  113,
  127,
  131,
  137,
  139,
  149,
  151,
  157,
  163,
  167,
  173,
  179,
  181,
  191,
  193,
  197,
  199,
  211,
  223,
  227,
  229,
  233,
  239,
  241,
  251,
  257,
  263,
  269,
  271,
  277,
  281,
  283,
  293,
  307,
  311,
  313,
  317,
  331,
  337,
  347,
  349,
  353,
  359,
  367,
  373,
  379,
  383,
  389,
  397,
  401,
  409,
  419,
  421,
  431,
  433,
  439,
  443,
  449,
  457,
  461,
  463,
  467,
  479,
  487,
  491,
  499,
  503,
  509,
  521,
  523,
  541,
  547,
  557,
  563,
  569,
  571,
  577,
  587,
  593,
  599,
  601,
  607,
  613,
  617,
  619,
  631,
  641,
  643,
  647,
  653,
  659,
  661,
  673,
  677,
  683,
  691,
  701,
  709,
  719,
  727,
  733,
  739,
  743,
  751,
  757,
  761,
  769,
  773,
  787,
  797,
  809,
  811,
  821,
  823,
  827,
  829,
  839,
  853,
  857,
  859,
  863,
  877,
  881,
  883,
  887,
  907,
  911,
  919,
  929,
  937,
  941,
  947,
  953,
  967,
  971,
  977,
  983,
  991,
  997,
  0};


static int isPrime(unsigned n)
{
	if (n == 2) return 1;
	if (n == 1 || (n & 1) == 0) return 0;
	unsigned int try = 1;
	for (unsigned const* p = primes; *p != 0; p++) {
		try = *p;
		if ((try * try) > n) {
			return 1;
		}
		if ((n % try) == 0) {
			return 0;
		}
	}
	die("Too large for isPrime; %u\n", n);
}


static unsigned primeBelow(unsigned n)
{
	if (n >= 994009)			/* 997^2 */
		n = 994009;
	if (n < 4) return n;
	if ((n & 1) == 0)
		n--;
	while (!isPrime(n)) {
		n -= 2;
	}
	return n;
}
