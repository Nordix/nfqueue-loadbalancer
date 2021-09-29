/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

#include "maglevdyn.h"
#include <prime.h>
#include <cmd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define Dx(x)
#define D(x)

static void targetAddRemove(unsigned M, unsigned N, unsigned A, float lim);
static int cmdTest(int argc, char **argv);

int main(int argc, char* argv[])
{
	srand(time(NULL));

	if (argc > 1)
		return cmdTest(argc, argv);

	unsigned int M=1000, N=100, len, i, j;
	struct MagDataDyn m;
	void* mem;
	unsigned* row;

	len = magDataDyn_len(M, N);
	mem = malloc(len);
	magDataDyn_init(M, N, mem, len);
	magDataDyn_map(&m, mem);
	Dx(printf("M=%u, N=%u, len=%u\n", m.M, m.N, len));
	assert(m.M == primeBelow(M));
	assert(m.N == N);
	for (i = 0; i < m.N; i++) {
		row = m.permutation[i];
		assert((void*)row - (void*)m.lookup < len);
		for (j = 0; j < m.M; j++) {
			assert(row[j] < m.M);
		}
		assert(m.active[i] == -1);
		assert(m.lookup[i] == -1);
	}
	m.active[0] = 100;
	magDataDyn_populate(&m);
	for (i = 0; i < m.N; i++) {
		assert(m.lookup[i] == 0);
	}
	m.active[1] = 101;
	magDataDyn_populate(&m);
	for (i = 0; i < m.N; i++) {
		assert(m.lookup[i] < 2);
	}
	free(mem);

	/*
	  These tests has a small probability to fail. It is natural since
	  they use random data. The proability seem to be < 1/1000. Test with;
	  i=0; while ./maglevdyn-test; do i=$((i+1)); echo $i; done
	 */
	targetAddRemove(109, 20, 10, 24.0); /* perfect = 10% */
	targetAddRemove(1009, 20, 10, 13.0); /* perfect = 10% */
	targetAddRemove(10009, 100, 50, 5.0); /* perfect = 2% */

	printf("==== maglevdyn-test OK\n");
	return 0;
}


static void* create(unsigned M, unsigned N)
{
	unsigned len = magDataDyn_len(M, N);
	void* mem = malloc(len);
	magDataDyn_init(M, N, mem, len);
	return mem;
}

static float addTargets(void* mem, unsigned n)
{
	struct MagDataDyn m;
	magDataDyn_map(&m, mem);
	// Save the lookup table
	int lookup[m.M];
	memcpy(lookup, m.lookup, sizeof(lookup));
	for (int i = 0; i < m.N && n > 0; i++) {
		if (m.active[i] < 0) {
			m.active[i] = 1;
			n--;
		}
	}
	magDataDyn_populate(&m);

	// Compute the update impact in percent
	unsigned ndiff = 0;
	for (int i = 0; i < m.M; i++) {
		if (lookup[i] != m.lookup[i])
			ndiff++;
	}
	return 100.0 * (float)ndiff / (float)m.M;
}

static float removeTargets(void* mem, unsigned n)
{
	struct MagDataDyn m;
	magDataDyn_map(&m, mem);
	// Save the lookup table
	int lookup[m.M];
	memcpy(lookup, m.lookup, sizeof(lookup));
	for (int i = 0; i < m.N && n > 0; i++) {
		if (m.active[i] >= 0) {
			m.active[i] = -1;
			n--;
		}
	}
	magDataDyn_populate(&m);

	// Compute the update impact in percent
	unsigned ndiff = 0;
	for (int i = 0; i < m.M; i++) {
		if (lookup[i] != m.lookup[i])
			ndiff++;
	}
	return 100.0 * (float)ndiff / (float)m.M;
}

static void targetAddRemove(
	unsigned M, unsigned N, unsigned A, float lim)
{
	float F;
	void* mem = create(M, N);
	addTargets(mem, A);
	F = addTargets(mem, 1);
	if (F >= lim)
		printf("addTargets M=%u, N=%u, A=%u; %.1f (%.1f)\n", M,N,A,F,lim);
	assert(F < lim);
	F = removeTargets(mem, 1);
	if (F >= lim)
		printf("addTargets M=%u, N=%u, A=%u; %.1f (%.1f)\n", M,N,A,F,lim);
	assert(F < lim);
	free(mem);
}

static int cmdTest(int argc, char **argv)
{
	char const* M = "997";
	char const* N = "32";
	char const* A = "10";
	char const* D = "1";
	struct Option options[] = {
		{"help", NULL, 0,
		 "test [options]\n"
		 "  Test MaglevDyn"},
		{"N", &N, 0, "Maglev max targets"},
		{"M", &M, 0, "Maglev lookup table size"},
		{"active", &A, 0, "Active targets"},
		{"D", &D, 0, "Targets to add/remove"},		
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);

	float F;
	void* mem = create(atoi(M), atoi(N));
	unsigned active = atoi(A);
	unsigned addrem = atoi(D);
	addTargets(mem, active);
	F = addTargets(mem, addrem);
	printf("add %u targets;    %.1f\n", addrem, F);
	F = removeTargets(mem, addrem);
	printf("remove %u targets; %.1f\n", addrem, F);
	free(mem);
	return 0;
}


