/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

#include "conntrack.h"
#include <cmd.h>
#include <die.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// Debug macros
#define Dx(x) x
#define D(x)

// Forwards
static void testConntrack(struct ctStats* stats);
static void testRefcount(struct ctStats* accumulatedStats);
static void testLimitedBuckets(struct ctStats* accumulatedStats);
static void testFreeDataFn(struct ctStats* accumulatedStats);

struct SustainedRateArg {
	unsigned duration;
	unsigned rate;
	unsigned ttl;
	unsigned hsize;
	unsigned buckets;
	unsigned seed;
	int assert;					/* Assert if packet loss > 1% */
};
static void* testSustainedRate(void *arg);

int
cmdCtBasic(int argc, char* argv[])
{
	char const* ft_size = "4000";
	char const* ft_buckets = "4000";
	char const* ft_ttl = "200";
	char const* repeat = "0";
	char const* parallel = "4";
	char const* duration = "0";
	char const* rate = "10000";
	struct Option options[] = {
		{"help", NULL, 0,
		 "ct-test [options]\n"
		 "  Connection/fragment tracker tests"},
		{"ft_size", &ft_size, 0, "Frag table size"},
		{"ft_buckets", &ft_buckets, 0, "Extra buckets"},
		{"ft_ttl", &ft_ttl, 0, "Ttl milliS"},
		{"repeat", &repeat, 0, "Repeat test"},
		{"parallel", &parallel, 0, "Parallel for repeated tests"},
		{"duration", &duration, 0, "Simulated test time in seconds"},
		{"rate", &rate, 0, "Rate in packets/S"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);
	srand(time(NULL));
	int rpt = atoi(repeat);
	
	struct SustainedRateArg sarg;
	sarg.duration = atoi(duration);
	sarg.rate = atoi(rate);
	sarg.ttl = atoi(ft_ttl);
	sarg.hsize = atoi(ft_size);
	sarg.buckets = atoi(ft_buckets);
	if (rpt > 0 && sarg.duration > 0) {
		unsigned j = atoi(parallel);
		unsigned i;
		sarg.assert = 0;
		if (j < 1) {
			for (i = 0; i < rpt; i++) {
				testSustainedRate(&sarg);
			}
			return 0;
		}
		pthread_t threads[j];
		while (rpt > 0) {
			int n = j;
			if (n > rpt)
				n = rpt;
			for (i = 0; i < n; i++) {
				if (pthread_create(
						&threads[i], NULL, testSustainedRate, &sarg) != 0)
					die("Failed to start pthread\n");
			}
			for (i = 0; i < n; i++) {
				void* retval;
				pthread_join(threads[i], &retval);
			}
			rpt -= n;
		}
		return 0;
	}

	struct ctStats stats = {0};
	testConntrack(&stats);
	testRefcount(&stats);
	testLimitedBuckets(&stats);
	testFreeDataFn(&stats);

	if (sarg.duration > 0) {
		sarg.assert = 1;
		testSustainedRate(&sarg);
	}

	printf(
		"==== ct-test OK. inserts=%u(%u) lookups=%u collisions=%u\n",
		stats.inserts, stats.rejectedInserts, stats.lookups, stats.collisions);
	return 0;
}


static long nAllocatedBuckets = 0;
static void* BUCKET_ALLOC(void* user_ref) {
	nAllocatedBuckets++;
	return calloc(1,sizeof_bucket);
}
static void BUCKET_FREE(void* user_ref, void* b) {
	nAllocatedBuckets--;
	free(b);
}

static unsigned nFreeData = 0;
static uint64_t expectedFreeData = 0;
static void freeData(void* user_ref, void* data) {
	D(printf("Free data; %lu\n", (uint64_t)data));
	nFreeData++;
	if (expectedFreeData != 0) {
		if ((uint64_t)data != expectedFreeData)
			printf(
				"Free data = %lu, expected = %lu\n",
				(uint64_t)data, expectedFreeData);
		assert((uint64_t)data == expectedFreeData);
	}
}

static void collectStats(
	struct ctStats* accumulatedStats, struct ctStats const* stats)
{
	accumulatedStats->collisions += stats->collisions;
	accumulatedStats->inserts += stats->inserts;
	accumulatedStats->rejectedInserts += stats->rejectedInserts;
	accumulatedStats->lookups += stats->lookups;
}

static void testConntrack(struct ctStats* accumulatedStats)
{
	struct ct* ct;
	struct timespec now = {0,0};
	struct ctKey key = {IN6ADDR_ANY_INIT,IN6ADDR_ANY_INIT,{0ull}};
	void* data;
	int rc;

	ctDestroy(NULL);			/* Should be OK */
	// Create table
	ct = ctCreate(
		1, 99, freeData, NULL, BUCKET_ALLOC, BUCKET_FREE, NULL);

	// Insert an empty key
	data = ctLookup(ct, &now, &key);
	assert(data == NULL);
	rc = ctInsert(ct, &now, &key, (void*)1001);
	assert(rc == 0);
	assert(nAllocatedBuckets == 0);
	data = ctLookup(ct, &now, &key);
	assert(data == (void*)1001);
	assert(ctStats(ct, &now)->active == 1);
	assert(nFreeData == 0);

	// Insert the same key again.
	nFreeData = 0;
	rc = ctInsert(ct, &now, &key, (void*)1002);
	assert(rc == 1);
	assert(nAllocatedBuckets == 0);
	assert(nFreeData == 0);
	data = ctLookup(ct, &now, &key);
	assert(data == (void*)1001);
	assert(ctStats(ct, &now)->active == 1);
	
	// The existing item should expire
	nFreeData = 0;
	expectedFreeData = 1001;
	now.tv_nsec += 100;
	rc = ctInsert(ct, &now, &key, (void*)1003);
	assert(rc == 0);
	assert(nFreeData == 1);
	assert(nAllocatedBuckets == 0);
	assert(ctStats(ct, &now)->active == 1);
	expectedFreeData = 0;

	// Cause a collision
	nFreeData = 0;
	key.id++;
	rc = ctInsert(ct, &now, &key, (void*)1004);
	assert(rc == 0);
	assert(nFreeData == 0);
	assert(nAllocatedBuckets == 1);
	assert(ctStats(ct, &now)->active == 2);
	assert(ctStats(ct, &now)->collisions == 1);

	// Insert a new item after some time
	nFreeData = 0;
	key.id++;
	now.tv_nsec += 50;
	rc = ctInsert(ct, &now, &key, (void*)1005);
	assert(rc == 0);
	assert(nFreeData == 0);
	assert(nAllocatedBuckets == 2);
	assert(ctStats(ct, &now)->active == 3);
	assert(ctStats(ct, &now)->collisions == 2);

	// Let the first 2 items expire then lookup the remaining
	nFreeData = 0;
	now.tv_nsec += 50;
	data = ctLookup(ct, &now, &key);
	assert(data == (void*)1005);
	assert(nAllocatedBuckets == 1);
	assert(nFreeData == 2);
	assert(ctStats(ct, &now)->active == 1);
	assert(ctStats(ct, &now)->collisions == 2);

	// The main bucket should be free. Insert and check nAllocatedBuckets
	nFreeData = 0;
	key.id++;
	rc = ctInsert(ct, &now, &key, (void*)1006);
	assert(rc == 0);
	data = ctLookup(ct, &now, &key);
	assert(data == (void*)1006);
	assert(nAllocatedBuckets == 1);
	assert(nFreeData == 0);
	assert(ctStats(ct, &now)->active == 2);
	assert(ctStats(ct, &now)->collisions == 3);

	// Remove the item in the "main" bucket
	nFreeData = 0;
	expectedFreeData = 1006;
	ctRemove(ct, &now, &key);
	assert(nFreeData == 1);
	assert(nAllocatedBuckets == 1);	
	data = ctLookup(ct, &now, &key);
	assert(data == NULL);
	assert(ctStats(ct, &now)->active == 1);
	assert(ctStats(ct, &now)->collisions == 3);

	// Flush the remaining item
	nFreeData = 0;
	expectedFreeData = 1005;
	assert(ctStats(ct, &now)->active == 1);
	now.tv_nsec += 100;
	assert(ctStats(ct, &now)->active == 0);
	assert(nFreeData == 1);

	// Insert 2 items with different TTL, step time and verify that
	// only one has expired
	key.id = 1007;
	rc = ctInsertWithTTL(ct, &now, &key, 200, (void*)key.id);
	assert(rc == 0);
	key.id = 1008;
	rc = ctInsertWithTTL(ct, &now, &key, 100, (void*)key.id);
	assert(rc == 0);
	assert(ctStats(ct, &now)->active == 2);
	now.tv_nsec += 150;
	nFreeData = 0;
	expectedFreeData = 1008;
	assert(ctStats(ct, &now)->active == 1);
	assert(nFreeData == 1);
	
	// Destroy the table. Remaining items shall be freed
	nFreeData = 0;
	expectedFreeData = 1007;
	collectStats(accumulatedStats, ctStats(ct, &now));
	ctDestroy(ct);
	assert(nFreeData == 1);
	assert(nAllocatedBuckets == 0);	

	// Test with a larger table
	ct = ctCreate(
		1000, 1000, freeData, NULL, BUCKET_ALLOC, BUCKET_FREE, NULL);
	now.tv_nsec = 0;
	key.id = 0;
	expectedFreeData = 0;
	nFreeData = 0;
	for (int i = 0; i < 1000; i++) {
		rc = ctInsert(ct, &now, &key, (void*)(key.id+1)); /* Don't use NULL! */
		assert(rc == 0);
		now.tv_nsec++;
		key.id++;
	}
	assert(nFreeData == 0);
	D(printf("Now = %lu, Active = %u\n",now.tv_nsec,ctStats(ct,&now)->active));
	assert(ctStats(ct, &now)->active == 1000);
	D(printf(
		  "allocated=%ld, collisions=%u\n",
		  nAllocatedBuckets, ctStats(ct, &now)->collisions));
	// NOTE; nAllocatedBuckets will change if a better hash function is used!!
	// (and BTW 766 is quite lousy)
	assert(nAllocatedBuckets == 766);
	assert(ctStats(ct, &now)->collisions == nAllocatedBuckets);
	now.tv_nsec += 500;
	D(printf("Now = %lu, Active = %u\n",now.tv_nsec,ctStats(ct,&now)->active));
	assert(ctStats(ct, &now)->active == 500);
	assert(nFreeData == 500);
	D(printf("allocated=%ld\n", nAllocatedBuckets));
	assert(nAllocatedBuckets == 384);
	collectStats(accumulatedStats, ctStats(ct, &now));
	ctDestroy(ct);
	assert(nFreeData == 1000);
}


#define REFINC(x) __atomic_add_fetch(&(x),1,__ATOMIC_SEQ_CST)
#define REFDEC(x) __atomic_sub_fetch(&(x),1,__ATOMIC_SEQ_CST)

struct FragData {
	int referenceCounter;
	unsigned id;
};
static unsigned allocatedFrags = 0;

static void lockFragData(void* user_ref, void* data)
{
	struct FragData* f = data;
	REFINC(f->referenceCounter);
}
static void unlockFragData(void* user_ref, void* data)
{
	struct FragData* f = data;
	if (REFDEC(f->referenceCounter) <= 0) {
		REFDEC(allocatedFrags);
		free(data);
	}
}
static struct FragData* allocFragData(unsigned id)
{
	struct FragData* f = malloc(sizeof(*f));
	REFINC(allocatedFrags);
	f->id = id;

	/*
	  If this is a "pure insert" the referenceCounter MUST be set to 1 (one).

	  If the item will be used in code that may have got the item by a
	  lookup, that code will do an unlock and the referenceCounter
	  MUST be set to 2 (two).
	 */
	f->referenceCounter = 1;		/* The ct refers it */
	return f;
}

static void testRefcount(struct ctStats* accumulatedStats)
{
	struct ct* ct;
	struct timespec now = {0,0};
	struct ctKey key = {IN6ADDR_ANY_INIT,IN6ADDR_ANY_INIT,{0ull}};
	int rc;
	struct FragData* f;

	ct = ctCreate(
		1000, 1000, unlockFragData, lockFragData, BUCKET_ALLOC, BUCKET_FREE, NULL);

	key.id = 1001;
	rc = ctInsert(ct, &now, &key, allocFragData(key.id));
	assert(rc == 0);
	assert(allocatedFrags == 1);
	f = ctLookup(ct, &now, &key);
	assert(f != NULL);
	assert(f->id == 1001);
	assert(f->referenceCounter == 2);
	assert(allocatedFrags == 1);
	ctRemove(ct, &now, &key);
	assert(f->referenceCounter == 1);
	assert(allocatedFrags == 1);
	unlockFragData(NULL, f);
	assert(allocatedFrags == 0);
	
	collectStats(accumulatedStats, ctStats(ct, &now));
	ctDestroy(ct);
}

struct bucketPool {
	// (mutex here for multi-treading)
	unsigned nfree;
	void** freeBuckets;
	uint8_t* buffer;
};

static void bucketPoolInit(struct bucketPool* p, unsigned size)
{
	D(printf("bucketPoolInit; %p\n", p));
	p->nfree = size;
	p->buffer = calloc(size, sizeof_bucket);
	p->freeBuckets = calloc(size, sizeof(void*));
	for (int i = 0; i < size; i++) {
		p->freeBuckets[i] = p->buffer + (i * sizeof_bucket);
	}
}
static void* bucketPoolAllocate(void* user_ref)
{
	D(printf("bucketPoolAllocate; %p\n", user_ref));
	struct bucketPool* p = user_ref;
	// lock
	if (p->nfree == 0) {
		// unlock
		return NULL;			/* Out of buckets */
	}
	p->nfree--;
	void* b = p->freeBuckets[p->nfree];
	// unlock
	return b;
}
static void bucketPoolFree(void* user_ref, void* b)
{
	struct bucketPool* p = user_ref;
	// lock
	p->freeBuckets[p->nfree] = b;
	p->nfree++;
	// unlock
}

static void testLimitedBuckets(struct ctStats* accumulatedStats)
{
	struct bucketPool bucketPool;
	struct ct* ct;
	struct timespec now = {0,0};
	struct ctKey key = {IN6ADDR_ANY_INIT,IN6ADDR_ANY_INIT,{0ull}};
	int rc;

	bucketPoolInit(&bucketPool, 2);
	assert(bucketPool.nfree == 2);
	ct = ctCreate(
		1, 100, NULL, NULL,
		bucketPoolAllocate, bucketPoolFree, &bucketPool);
	key.id = 1001;
	rc = ctInsert(ct, &now, &key, (void*)key.id);
	assert(rc == 0);
	assert(bucketPool.nfree == 2);

	key.id = 1002;
	rc = ctInsert(ct, &now, &key, (void*)key.id);
	assert(rc == 0);
	assert(bucketPool.nfree == 1);

	key.id = 1003;
	rc = ctInsert(ct, &now, &key, (void*)key.id);
	assert(rc == 0);
	assert(bucketPool.nfree == 0);

	key.id = 1004;
	rc = ctInsert(ct, &now, &key, (void*)key.id);
	assert(rc == -1);
	assert(bucketPool.nfree == 0);

	key.id = 1001;
	ctRemove(ct, &now, &key);
	assert(bucketPool.nfree == 0);
	
	key.id = 1005;
	rc = ctInsert(ct, &now, &key, (void*)key.id);
	assert(rc == 0);
	assert(bucketPool.nfree == 0);

	key.id = 1006;
	rc = ctInsert(ct, &now, &key, (void*)key.id);
	assert(rc == -1);
	assert(bucketPool.nfree == 0);

	key.id = 1003;
	ctRemove(ct, &now, &key);
	assert(bucketPool.nfree == 1);
	
	collectStats(accumulatedStats, ctStats(ct, &now));
	ctDestroy(ct);
	// Allocated buckets shall be freed on "ctDestroy"
	assert(bucketPool.nfree == 2);
}

/*
  Added to verify a bugfix in ctRemove
 */
static void testFreeDataFn(struct ctStats* accumulatedStats)
{
	struct ct* ct;
	struct timespec now = {0,0};
	struct ctKey key = {IN6ADDR_ANY_INIT,IN6ADDR_ANY_INIT,{0ull}};
	int rc;

	ct = ctCreate(
		1, 100, freeData, NULL, BUCKET_ALLOC, BUCKET_FREE, NULL);
	key.id = 1001;
	rc = ctInsert(ct, &now, &key, (void*)key.id);
	assert(rc == 0);

	key.id = 1002;
	rc = ctInsert(ct, &now, &key, (void*)key.id);
	assert(rc == 0);

	key.id = 1002;
	expectedFreeData = key.id;
	ctRemove(ct, &now, &key);
	expectedFreeData = 0;

	collectStats(accumulatedStats, ctStats(ct, &now));
	ctDestroy(ct);
}

/* ----------------------------------------------------------------------
   Sustained rate tests
*/
static pthread_mutex_t printmutex = PTHREAD_MUTEX_INITIALIZER;
struct UserRef {
	unsigned maxBuckets;
	unsigned bucketsPeak;
	unsigned nAllocatedBuckets;
	unsigned nFreeData;
};
static void* bucket_alloc(void* user_ref) {
	struct UserRef* userRef = user_ref;
	if (userRef->nAllocatedBuckets >= userRef->maxBuckets)
		return NULL;
	userRef->nAllocatedBuckets++;
	if (userRef->nAllocatedBuckets > userRef->bucketsPeak)
		userRef->bucketsPeak = userRef->nAllocatedBuckets;
	return calloc(1,sizeof_bucket);
}
static void bucket_free(void* user_ref, void* b) {
	struct UserRef* userRef = user_ref;
	userRef->nAllocatedBuckets--;
	free(b);
}
static void data_free(void* user_ref, void* b) {
	struct UserRef* userRef = user_ref;
	userRef->nFreeData++;
}

/*
  With ttl=0.2s and sustained rate of 10000 pkt/sec we test the formula;

    hsize = rate * ttl * C
	maxBuckets = hsize

  C = 2 is supposed to give rough recommended sizes.
 */
#define MS 1000000ul
#define SEC 1000000000ul
static uint64_t timeToNextPacket(unsigned rate, unsigned* seed);
static void* testSustainedRate(void *_arg)
{
	struct SustainedRateArg* arg = _arg;
	struct ct* ct;
	struct timespec now = {0,0};
	struct ctKey key;
	uint64_t nowNanos = 0;
	uint64_t duration = (uint64_t)arg->duration * SEC;
	struct ctStats stats;
	struct UserRef userRef = {0};
	unsigned seed;

	pthread_mutex_lock(&printmutex);
	seed = rand();
	pthread_mutex_unlock(&printmutex);

	// init variables
	nowNanos = 0;
	memset(&key, 0, sizeof(key));

	// Create table
	userRef.maxBuckets = arg->buckets;
	ct = ctCreate(
		arg->hsize, arg->ttl*MS,
		data_free, NULL, bucket_alloc, bucket_free, &userRef);
	ctUseStats(ct, &stats);

	/*
	  Real-life test programs (e.g iperf) are connects to the vip with
	  a few different source addresses.  Basically the only thing
	  changing is the fragid. And it is probably just incremented.
	 */
	
	// Simulated time
	key.id = rand_r(&seed);
	while (nowNanos < duration) {
		nowNanos += timeToNextPacket(arg->rate, &seed);
		now.tv_sec = nowNanos / SEC;
		now.tv_nsec = nowNanos % SEC;
		key.id = (key.id + 1);
		ctInsert(ct, &now, &key, (void*)(key.id));
	}

	unsigned beforeFullGC = stats.objGC;
	(void)ctStats(ct, &now);
	double percentLoss = 0;
	if (stats.inserts > 0)
		percentLoss =
			(double)stats.rejectedInserts * 100.0 / (double)stats.inserts;
	if (arg->assert) {
		if (percentLoss > 1.0)
			printf("Packet loss; %2.1f%%\n", percentLoss);
		assert(percentLoss <= 1.0);
	} else {
		pthread_mutex_lock(&printmutex);
		printf(
            "{\n"
            "  \"ttlMillis\":     %u,\n"
            "  \"size\":          %u,\n"
            "  \"active\":        %u,\n"
            "  \"collisions\":    %u,\n"
            "  \"inserts\":       %u,\n"
            "  \"rejected\":      %u,\n"
            "  \"lookups\":       %u,\n"
            "  \"objGC\":         %u,\n"
            "  \"bucketsMax\":    %u,\n"
            "  \"bucketsPeak\":   %u,\n"
            "  \"bucketsStale\":  %u,\n"
            "  \"percentLoss\":   %2.1f\n"
            "}\n",
			(unsigned)(stats.ttlNanos / MS), stats.size, stats.active,
			stats.collisions, stats.inserts, stats.rejectedInserts,
			stats.lookups, stats.objGC,
			userRef.maxBuckets, userRef.bucketsPeak, stats.objGC - beforeFullGC,
			percentLoss);
		pthread_mutex_unlock(&printmutex);
	}

	// Destroy table
	unsigned nData = stats.inserts - stats.rejectedInserts;
	ctDestroy(ct);
	assert(userRef.nAllocatedBuckets == 0);
	assert(userRef.nFreeData == nData);
	return NULL;
}
static uint64_t timeToNextPacket(unsigned rate, unsigned* seed)
{
	uint64_t d = SEC / rate;
	// Randomize this some
	return rand_r(seed) % (d * 2);
}

#ifdef CMD
void addCmd(char const* name, int (*fn)(int argc, char* argv[]));
__attribute__ ((__constructor__)) static void addCommand(void) {
        addCmd("ct_basic", cmdCtBasic);
}
#else
int main(int argc, char* argv[])
{
	return cmdCtBasic(argc, argv);
}
#endif

