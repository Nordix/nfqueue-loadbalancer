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
	return sizeof(struct MagDataDynInternal)  + sizeof(unsigned) * (3 + M * N)
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
	for (int i = 0; i < m.N; i++) {
		unsigned offset = rand() % m.M;
		unsigned skip = rand() % (m.M - 1) + 1;
		unsigned* row = m.permutation[i];
		for (unsigned j = 0; j < m.M; j++) {			
			row[j] = (offset + j * skip) % m.M;
		}
		m.active[i] = -1;
	}
	magDataDyn_populate(&m);
	magDataDyn_free(&m);
}

void magDataDyn_map(struct MagDataDyn* m, void* mem)
{
	struct MagDataDynInternal* mi = mem;
	m->M = mi->M;
	m->N = mi->N;
	unsigned offset = sizeof(struct MagDataDynInternal);
	m->lookup = mem + offset;
	offset += (m->M * sizeof(int));
	m->permutation = malloc(m->N * sizeof(unsigned*));
	if (m->permutation == NULL)
		die("Out of mem\n");
	unsigned i;
	for (i = 0; i < m->N; i++) {
		m->permutation[i] = mem + offset;
		offset += (m->M * sizeof(unsigned));
	}
	m->active = mem + offset;	   
}
void magDataDyn_free(struct MagDataDyn* m)
{
	free(m->permutation);
}

void magDataDyn_populate(struct MagDataDyn* d)
{
	for (int i = 0; i < d->M; i++) {
		d->lookup[i] = -1;
	}

	// Corner case; no active targets
	unsigned nActive = 0;
	for (int i = 0; i < d->N; i++) {
		if (d->active[i] >= 0) nActive++;
	}
	if (nActive == 0) return;

	unsigned next[d->N], c = 0;
	memset(next, 0, sizeof(next));
	unsigned n = 0;
	unsigned* row;
	for (;;) {
		for (int i = 0; i < d->N; i++) {
			if (d->active[i] < 0) continue; /* Target not active */
			row = d->permutation[i];
			c = row[next[i]];
			while (d->lookup[c] >= 0) {
				next[i] = next[i] + 1;
				c = row[next[i]];
			}
			d->lookup[c] = i;
			next[i] = next[i] + 1;
			n = n + 1;
			if (n == d->M) return;
		}
	}
}
