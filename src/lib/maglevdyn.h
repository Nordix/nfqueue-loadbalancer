#pragma once

struct MagDataDyn {
	unsigned M, N;
	int* lookup;
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

/*
  Call when the "active" array is updated
 */
void magDataDyn_populate(struct MagDataDyn* m);


// This is equivalent to
// currentValue - skip >= 0 ? currentValue - skip : currentValue - skip + mod
// but much faster than any conditional branching. In fact, this is as fast
// as retrieving the values from a pre-computed table.
static inline int compute_next_element_in_permutation(int currentValue, int skip, int mod)
{
	int v = currentValue - skip;
	v += mod & ((v >= 0) - 1);
	return v;
}
