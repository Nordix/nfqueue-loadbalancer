#pragma once

struct MagDataDyn {
	unsigned M, N;
	int *lookup;
	unsigned** permutation;
	int* active;
};

/*
  Returns the minimum length of memory.
 */
unsigned magDataDyn_len(unsigned M, unsigned N);

/*
  M will be adjusted to a prime lower than the passed value if needed
  and max 994009.

  Prerequisite; mem allocated and len >= returned by magDataDyn_len()
 */
void magDataDyn_init(unsigned M, unsigned N, void* mem, unsigned len);

/*
  Map to a memory area that may be in shared mem.
  Must call magDataDyn_free() to free allocated memory.
 */
void magDataDyn_map(struct MagDataDyn* m, void* mem);
void magDataDyn_free(struct MagDataDyn* m);

/*
  Call when the "active" array is updated
 */
void magDataDyn_populate(struct MagDataDyn* m);
