/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021-2022 Nordix Foundation
*/

#include "reassembler.h"
#include <assert.h>
#include <stdio.h>

int handleFragment(
	struct Item* hitem, unsigned offset, unsigned len, int morefragments);

#define HOLES(p) (p->size - p->nFree - 1)

int
main(int argc, char* argv[])
{
	int rc;
	struct FragReassembler* ra = createReassembler(100);
	assert(ra != NULL);
	struct ReassemblerStats const* raStats = getReassemblerStats();
	assert(raStats->pool->size == 100);

	void* r = ra->new();
	assert(r != NULL);
	assert(HOLES(raStats->pool) == 1);
	ra->destroy(r);
	assert(raStats->pool->nFree == 100);

	r = ra->new();
	rc = handleFragment(r, 0, 100, 1);
	assert(rc == 1);
	assert(HOLES(raStats->pool) == 1);
	rc = handleFragment(r, 100, 100, 0);
	assert(rc == 0);
	assert(HOLES(raStats->pool) == 0);
	ra->destroy(r);

	r = ra->new();
	rc = handleFragment(r, 100, 100, 0);
	assert(rc == 1);
	assert(HOLES(raStats->pool) == 1);
	rc = handleFragment(r, 0, 100, 1);
	assert(rc == 0);
	assert(HOLES(raStats->pool) == 0);
	ra->destroy(r);

	r = ra->new();
	rc = handleFragment(r, 100, 100, 1);
	assert(rc == 1);
	assert(HOLES(raStats->pool) == 2);
	rc = handleFragment(r, 0, 100, 1);
	assert(HOLES(raStats->pool) == 1);
	assert(rc == 1);
	rc = handleFragment(r, 200, 100, 0);
	assert(rc == 0);
	assert(HOLES(raStats->pool) == 0);
	ra->destroy(r);
	
	printf("==== reassembler-test OK\n");
	return 0;
}

