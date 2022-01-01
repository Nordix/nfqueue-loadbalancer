/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021-2022 Nordix Foundation
*/

/*
  The packet handling functions, ipv6Fragment() and ipv4Fragment(),
  are tested in test-pcap.c.
*/

#include "fragutils.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#define MS 1000000				/* One milli second in nanos */

#define xstr(a) str(a)
#define str(a) #a
#define S_CMP(x) if (a->x != b->x) { rc = 1; \
		printf("a.%s = %u; != %u\n", xstr(x), b->x, a->x); }

static int statsCmp(struct fragStats* a, struct fragStats* b)
{
	int rc = 0;
	S_CMP(ctstats.size);
	S_CMP(ctstats.active);
	S_CMP(ctstats.collisions);
	S_CMP(ctstats.inserts);
	S_CMP(ctstats.rejectedInserts);
	S_CMP(ctstats.lookups);
	S_CMP(ctstats.objGC);
	S_CMP(bucketsMax);
	S_CMP(fragsMax);
	S_CMP(mtu);
	S_CMP(fragsAllocated);
	S_CMP(fragsDiscarded);
	return rc;
}

static int numItems(struct Item* items)
{
	int i = 0;
	while (items != NULL) {
		i++;
		items = items->next;
	}
	return i;
}

static int reass_data = -1;
static void* reass_new(void)
{
	assert(reass_data == -1);
	reass_data = 0;
	return &reass_data;
}
static int reass_handleFragment(void* r, void const* data, unsigned len)
{
	assert(r == &reass_data);
	assert(reass_data >= 0);
	reass_data++;
	if (reass_data >= 3)
		return 0;
	return 1;
}
static void reass_destoy(void* r)
{
	assert(r == &reass_data);
	assert(reass_data >= 0);
	reass_data = -1;
}


int
cmdFragutilsBasic(int argc, char* argv[])
{
	struct timespec now = {0,0};
	struct fragStats a;
	struct fragStats b;
	struct ctKey key = {IN6ADDR_ANY_INIT,IN6ADDR_ANY_INIT,{0ull}};
	int rc;
	int hash;
	struct Item* item;
	struct FragTable* ft;

	// Init and check stats
	ft = fragTableCreate(2, 3, 4, 1500, 100);
	fragGetStats(ft, &now, &a);
	memset(&b, 0, sizeof(b));
	b.ctstats.ttlNanos = 100*MS;
	b.ctstats.size = 2;
	b.bucketsMax = 3;
	b.fragsMax = 4;
	b.mtu = 1500;
	assert(statsCmp(&a, &b) == 0);

	// Unsuccesful lookup
	fragGetStats(ft, &now, &a);
	a.ctstats.lookups++;
	rc = fragGetValue(ft, &now, &key, &hash);
	assert(rc == -1);
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);

	// Insert a first-fragment and look it up
	fragGetStats(ft, &now, &a);
	a.ctstats.lookups++;
	a.ctstats.inserts++;
	a.ctstats.active++;
	rc = fragInsertFirst(ft, &now, &key, 5, NULL, NULL, 0);
	assert(rc == 0);
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);

	fragGetStats(ft, &now, &a);
	a.ctstats.lookups++;
	rc = fragGetValue(ft, &now, &key, &hash);
	assert(rc == 0);
	assert(hash == 5);
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);

	// Step time and check GC
	fragGetStats(ft, &now, &a);
	a.ctstats.active = 0;
	a.ctstats.objGC++;
	now.tv_nsec += 150 * MS;
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);

	// Add 3 sub-frags
	fragGetStats(ft, &now, &a);
	a.ctstats.active++;
	a.ctstats.lookups++;
	a.ctstats.inserts++;
	a.fragsAllocated++;
	rc = fragGetValueOrStore(ft, &now, &key, &hash, &key, sizeof(key));
	assert(rc == 1);
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);

	fragGetStats(ft, &now, &a);
	a.ctstats.lookups++;
	a.fragsAllocated++;
	rc = fragGetValueOrStore(ft, &now, &key, &hash, &key, sizeof(key));
	assert(rc == 1);
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);

	fragGetStats(ft, &now, &a);
	a.ctstats.lookups++;
	a.fragsAllocated++;
	rc = fragGetValueOrStore(ft, &now, &key, &hash, &key, sizeof(key));
	assert(rc == 1);
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);

	// Step time and check that the (3) fragments are discarded
	fragGetStats(ft, &now, &a);
	a.ctstats.active = 0;
	a.ctstats.objGC++;
	a.fragsDiscarded = 3;
	now.tv_nsec += 150 * MS;
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);

	// Add 3 sub-frags #2
	fragGetStats(ft, &now, &a);
	a.ctstats.active++;
	a.ctstats.lookups++;
	a.ctstats.inserts++;
	a.fragsAllocated++;
	rc = fragGetValueOrStore(ft, &now, &key, &hash, &key, sizeof(key));
	assert(rc == 1);
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);

	fragGetStats(ft, &now, &a);
	a.ctstats.lookups++;
	a.fragsAllocated++;
	rc = fragGetValueOrStore(ft, &now, &key, &hash, &key, sizeof(key));
	assert(rc == 1);
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);

	fragGetStats(ft, &now, &a);
	a.ctstats.lookups++;
	a.fragsAllocated++;
	rc = fragGetValueOrStore(ft, &now, &key, &hash, &key, sizeof(key));
	assert(rc == 1);
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);

	// Get and release the stored fragments
	fragGetStats(ft, &now, &a);
	a.ctstats.lookups++;
	rc = fragInsertFirst(ft, &now, &key, 444, &item, NULL, 0);
	assert(item != NULL);
	assert(numItems(item) == 3);
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);
	itemFree(item);
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);

	// Clear the table
	now.tv_nsec += 150 * MS;
	a.ctstats.active = 0;
	a.ctstats.objGC++;
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);

	// Add fragments until we run out of space and make sure stored
	// fragments are discarded and that no new fragments are stored.
	fragGetStats(ft, &now, &a);
	a.ctstats.active++;
	a.ctstats.lookups++;
	a.ctstats.inserts++;
	a.fragsAllocated++;
	rc = fragGetValueOrStore(ft, &now, &key, &hash, &key, sizeof(key));
	assert(rc == 1);
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);

	fragGetStats(ft, &now, &a);
	a.ctstats.lookups++;
	a.fragsAllocated++;
	rc = fragGetValueOrStore(ft, &now, &key, &hash, &key, sizeof(key));
	assert(rc == 1);
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);

	fragGetStats(ft, &now, &a);
	a.ctstats.lookups++;
	a.fragsAllocated++;
	rc = fragGetValueOrStore(ft, &now, &key, &hash, &key, sizeof(key));
	assert(rc == 1);
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);

	fragGetStats(ft, &now, &a);
	a.ctstats.lookups++;
	a.fragsAllocated++;
	rc = fragGetValueOrStore(ft, &now, &key, &hash, &key, sizeof(key));
	assert(rc == 1);
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);

	// Now the frag store is full

	fragGetStats(ft, &now, &a);
	a.ctstats.lookups++;
	a.fragsDiscarded += 4;
	rc = fragGetValueOrStore(ft, &now, &key, &hash, &key, sizeof(key));
	assert(rc == -1);
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);

	// Try to store more fragments once we have lost one should fail
	fragGetStats(ft, &now, &a);
	a.ctstats.lookups++;
	rc = fragGetValueOrStore(ft, &now, &key, &hash, &key, sizeof(key));
	assert(rc == -1);
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);

	// Also try to add first-fragment should fail
	fragGetStats(ft, &now, &a);
	a.ctstats.lookups++;
	rc = fragInsertFirst(ft, &now, &key, 444, &item, NULL, 0);
	assert(rc == -1);
	assert(item == NULL);
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);

	fragTableDestroy(ft);

	/*
	  Re-assembler tests
	 */

	ft = fragTableCreate(2, 3, 4, 1500, 100);
	fragGetStats(ft, &now, &a);
	fragGetStats(ft, &now, &b);
	assert(statsCmp(&a, &b) == 0);
	struct FragReassembler reass = {
		reass_new, reass_handleFragment, reass_destoy
	};
	fragRegisterFragReassembler(ft, &reass);

	rc = fragInsertFirst(ft, &now, &key, 5, NULL, NULL, 0);
	assert(reass_data == 1);
	rc = fragGetValueOrStore(ft, &now, &key, &hash, &key, sizeof(key));
	assert(reass_data == 2);
	rc = fragGetValueOrStore(ft, &now, &key, &hash, &key, sizeof(key));
	assert(reass_data == -1);
	fragGetStats(ft, &now, &b);
	a.ctstats.inserts = 1;
	a.ctstats.lookups = 3;
	a.reAssembled = 1;
	assert(statsCmp(&a, &b) == 0);

	// When we must store packets the reassembler should be discarded
	rc = fragGetValueOrStore(ft, &now, &key, &hash, &key, sizeof(key));
	assert(reass_data == -1);
	rc = fragGetValueOrStore(ft, &now, &key, &hash, &key, sizeof(key));
	assert(reass_data == -1);
	rc = fragInsertFirst(ft, &now, &key, 5, NULL, NULL, 0);
	assert(reass_data == -1);
	fragGetStats(ft, &now, &b);
	a.ctstats.inserts += 1;
	a.ctstats.lookups += 3;
	a.fragsAllocated = 2;
	a.fragsDiscarded = 2;
	a.ctstats.active = 1;
	assert(statsCmp(&a, &b) == 0);

	fragTableDestroy(ft);
	
	printf("==== fragutils-test OK\n");
	return 0;
}


#ifdef CMD
void addCmd(char const* name, int (*fn)(int argc, char* argv[]));
__attribute__ ((__constructor__)) static void addCommand(void) {
	addCmd("fragutils_basic", cmdFragutilsBasic);
}
#else
int main(int argc, char* argv[])
{
	return cmdFragutilsBasic(argc, argv);
}
#endif
