#include "maglevdyn.h"
#include <die.h>
#include <prime.h>

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

struct MagDataDynInternal {
	unsigned M, N;
	unsigned len;
	uint8_t mem[];				/* Actuall size=len */
};

unsigned magDataDyn_len(unsigned M, unsigned N)
{
	if (M < 3)
		M = 3;
	M = primeBelow(M);
	return sizeof(struct MagDataDynInternal) + sizeof(unsigned) * 3
		+ sizeof(int) * (M + N);
}

void magDataDyn_init(unsigned M, unsigned N, void* mem, unsigned len)
{
	if (M < 3)
		M = 3;
	M = primeBelow(M);
	if (len < magDataDyn_len(M, N))
		die("magDataDyn len too small; %u < %u\n", len, magDataDyn_len(M, N));

	memset(mem, 0, len);
	struct MagDataDynInternal* mi = mem;
	mi->M = M;
	mi->N = N;
	mi->len = len;

	struct MagDataDyn m;
	magDataDyn_map(&m, mem);
	memset(m.active, 0xff, sizeof(int)*N);
	memset(m.lookup, 0xff, sizeof(int)*M);
}

void magDataDyn_map(struct MagDataDyn* m, void* mem)
{
	struct MagDataDynInternal* mi = mem;
	m->M = mi->M;
	m->N = mi->N;
	m->lookup = mem + sizeof(struct MagDataDynInternal);
	m->active = mem + sizeof(struct MagDataDynInternal) + sizeof(int) * m->M;
}

struct ActiveTarget {
	int skip;
	int c;
	int idx;
};


void magDataDyn_populate(struct MagDataDyn* d)
{
	struct ActiveTarget* activeTargets = (struct ActiveTarget*) malloc(d->N * sizeof(struct ActiveTarget));
	if (activeTargets == NULL) die("Out of memory activeTargets");
	int num_targets = 0;

	for (int i = 0; i < d->N; i++) {
		int offset = rand();
		int skip = rand();
		if (d->active[i] >= 0) {
			activeTargets[num_targets].idx = i;
			activeTargets[num_targets].c = offset % d->M;
			// The old algorithm went "upwards" with random skip values in [1, M-1]
			// and the next element in the permutation table was given by current+skip % M
			// But we can compute the next element much faster if we go "downwards",
			// using another skip' value such that skip + skip' == M
			// The two methods yield the same permutation sequence.
			activeTargets[num_targets].skip = d->M - ((skip % (d->M - 1)) + 1);
			num_targets++;
		}
	}
	if (num_targets < 2) { // Corner cases: no active targets or just 1 active target
		int w = num_targets == 0 ? -1 : activeTargets[0].idx;
		for (int i = 0; i < d->M; i++) {
			d->lookup[i] = w;
		}
		free(activeTargets);
		return;
	}

	int* tmpLookup = (int*) malloc(d->M * sizeof(int));
	if (tmpLookup == NULL) die ("Out of memory tmpLookup");
	memset(tmpLookup, 0xff, sizeof(int)*d->M);

	int k = 0;
	for (int n = 0; n < d->M; n++) {
		int c = activeTargets[k].c;
		while (tmpLookup[c] >= 0) {
			c = compute_next_element_in_permutation(c, activeTargets[k].skip, d->M);
		}
		tmpLookup[c] = activeTargets[k].idx;
		activeTargets[k].c = c = compute_next_element_in_permutation(c, activeTargets[k].skip, d->M);
		k = k < num_targets - 1 ? k+1 : 0;
	}
	memcpy(d->lookup, tmpLookup, sizeof(int) * d->M);
	free(tmpLookup);
	free(activeTargets);
}
