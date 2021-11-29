/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

#include <rangeset.h>
#include <die.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

// Debug macros
#ifdef VERBOSE
#define Dx(x) x
#else
#define Dx(x)
#endif
#define D(x)

int main(int argc, char* argv[])
{
	// Basic
	rangeSetDestroy(NULL);
	struct RangeSet* t;
	t = rangeSetCreate();
	assert(t != NULL);
	assert(rangeSetSize(t) == 0);
	rangeSetUpdate(t);
	assert(rangeSetIn(t, 0) == 0);
	rangeSetDestroy(t);

	// String parsing
	t = rangeSetCreate();
	assert(rangeSetAddStr(t, "10") == 0);
	assert(rangeSetAddStr(t, "10, 11") == 0);
	assert(rangeSetAddStr(t, "10, 11, 20-30") == 0);
	assert(rangeSetAddStr(t, "10,11,,, ") == 0);
	assert(rangeSetAddStr(t, "0-4294967295") == 0);
	assert(rangeSetSize(t) == 9); /* (duplicates counts before update) */
	assert(rangeSetAddStr(t, "nope") != 0);
	assert(rangeSetAddStr(t, "200-100") != 0);
	assert(rangeSetSize(t) == 9);
	rangeSetDestroy(t);

	// Argv parsing
	t = rangeSetCreate();
	char const* a01[] = {
		NULL
	};
	assert(rangeSetAddArgv(t, NULL) == 0);
	assert(rangeSetAddArgv(t, a01) == 0);
	char const* a02[] = {
		"20-30", "10", "11", "20-30",
		NULL
	};
	assert(rangeSetAddArgv(t, a02) == 0);
	assert(rangeSetSize(t) == 4);
	rangeSetUpdate(t);
	assert(rangeSetSize(t) == 2);
	rangeSetDestroy(t);

	// Update
	t = rangeSetCreate();
	assert(rangeSetAddStr(t, "20-30, 10, 11, 20-30") == 0);
	assert(rangeSetAddStr(t, "18-20") == 0);
	assert(rangeSetAddStr(t, "10") == 0);
	assert(rangeSetSize(t) == 6);
	rangeSetUpdate(t);
	assert(rangeSetSize(t) == 2);
	assert(rangeSetAddStr(t, "0-4294967295") == 0);
	rangeSetUpdate(t);
	assert(rangeSetSize(t) == 1);
	rangeSetDestroy(t);

	// Basic search
	t = rangeSetCreate();
	assert(rangeSetAddStr(t, "0-10, 12-20, 55") == 0);
	rangeSetUpdate(t);
	assert(rangeSetIn(t, 10));
	assert(!rangeSetIn(t, 11));
	assert(rangeSetIn(t, 12));
	assert(!rangeSetIn(t, 21));
	assert(rangeSetIn(t, 55));
	assert(!rangeSetIn(t, 56));
	rangeSetDestroy(t);

	// Tree string
	char buf[512];
	t = rangeSetCreate();
	assert(rangeSetAddStr(t, "55, 12-20, 0-10, 13, 5,6, 6-9") == 0);
	rangeSetUpdate(t);
	assert(rangeSetString(t, buf, sizeof(buf)) == 0);
	D(printf("%s\n", buf));
	assert(strcmp(buf, "0-10,12-20,55,") == 0);
	rangeSetDestroy(t);

	// Depth test
	extern unsigned rangeTreeDepth(struct RangeSet* t);
	t = rangeSetCreate();
	assert(rangeTreeDepth(t) == 0);
	srand(time(NULL));
	for (int i = 0; i < 256; i++) {
		unsigned v = rand() % 1000;
		assert(rangeSetAdd(t, v, v) == 0);
	}
	rangeSetUpdate(t);
	D(printf("cnt=%u, depth=%u\n", rangeSetSize(t), rangeTreeDepth(t)));
	assert(rangeTreeDepth(t) <= 8);
	rangeSetDestroy(t);

	// Tree for show and interactive tests
	extern void rangeTreePrint(struct RangeSet* t);
	unsigned seed = time(NULL);
	if (argc > 1)
		seed = atoi(argv[1]);
	Dx(printf("seed=%u\n", seed));
	srand(seed);
	t = rangeSetCreate();
	assert(rangeTreeDepth(t) == 0);
	for (int i = 0; i < 256; i++) {
		unsigned v = rand() % 120;
		assert(rangeSetAdd(t, v, v) == 0);
	}
	rangeSetUpdate(t);
	Dx(printf("cnt=%u, depth=%u\n", rangeSetSize(t), rangeTreeDepth(t)));
	Dx(rangeTreePrint(t));
	for (int i = 2; i < argc; i++) {
		assert(rangeSetIn(t, atoi(argv[i])));
	}
	rangeSetDestroy(t);

	// Limited range set
	assert(rangeSetCreateLimited(1, 1) == NULL);
	assert(rangeSetCreateLimited(2, 1) == NULL);
	t = rangeSetCreateLimited(10, 20);
	assert(t != NULL);
	assert(rangeSetAddStr(t, "9") != 0);
	assert(rangeSetAddStr(t, "10") == 0);
	assert(rangeSetAddStr(t, "20") == 0);
	assert(rangeSetAddStr(t, "21") != 0);
	assert(rangeSetAddStr(t, "10-20") == 0);
	rangeSetDestroy(t);
	
	printf("==== rangeset-test OK.\n");
	return 0;
}
