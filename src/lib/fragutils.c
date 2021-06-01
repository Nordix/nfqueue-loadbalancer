/*
   SPDX-License-Identifier: Apache-2.0
   Copyright (c) 2021 Nordix Foundation
*/

#include "fragutils.h"
#include <pthread.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef SANITY_CHECK
#else
#define assert(x)
#endif

#define MS 1000000				/* One milli second in nanos */

#define REFINC(x) __atomic_add_fetch(&(x),1,__ATOMIC_SEQ_CST)
#define REFDEC(x) __atomic_sub_fetch(&(x),1,__ATOMIC_SEQ_CST)
#define CNTINC(x) __atomic_add_fetch(&(x),1,__ATOMIC_RELAXED)
#define CNTDEC(x) __atomic_sub_fetch(&(x),1,__ATOMIC_RELAXED)
#define MUTEX(x) pthread_mutex_t x
#define LOCK(x) pthread_mutex_lock(x)
#define UNLOCK(x) pthread_mutex_unlock(x)
#define MUTEX_DESTROY(x) pthread_mutex_destroy(x)
#define MUTEX_INIT(x) pthread_mutex_init(x, NULL);

/* ----------------------------------------------------------------------
 */

/*
  Holds data necessary to store hash values and fragments to re-inject
  in the ct.
*/
struct FragTable {
	struct ct* ct;
	struct ItemPool* fragDataPool; /* Items actually stored in the ct */
	struct ItemPool* bucketPool;   /* Extra buckets on hash collisions */
	struct ItemPool* fragmentPool; /* Stored not-first fragments to re-inject */
	struct fragStats _fstats;
	struct fragStats* fstats;
};

/*
  FragData objects are stored in the ct. Since these are returned in a
  ctLookup() we must ensure that the object is not released by another
  thread while the ctLookup() caller is using it. So, a
  reference-counter is used.
*/
struct FragData {
	int referenceCounter;
	MUTEX(mutex);
	int firstFragmentSeen;		/* Meaning the hash is valid */
	unsigned hash;
	struct Item* storedFragments;
};


// user_ref may be NULL
static void fragDataLock(void* user_ref, void* data)
{
	struct FragData* f = data;
	REFINC(f->referenceCounter);
}
// user_ref may be NULL
static void fragDataUnlock(void* user_ref, void* data)
{
	struct FragData* f = data;
	if (REFDEC(f->referenceCounter) <= 0) {
		/*
		  If the FragData object is released due to a timeout there
		  may be stored fragments lingering. Normally these should
		  have been re-injected and freed by now.
		 */
		struct Item* i;
		struct FragTable* ft = user_ref;
		for (i = f->storedFragments; i != NULL; i = i->next)
			CNTINC(ft->fstats->fragsDiscarded);
		itemFree(f->storedFragments);
		struct Item* item = ITEM_OF(data);
		itemFree(item);
	}
}
/*
  Lookup or create a FragData object.
  return;
  NULL - No more buckets (or something really weird)
  != NULL - FragData. The calling function *must* call fragDataUnlock!
*/
static struct FragData* fragDataLookup(
	struct FragTable* ft, struct timespec* now, struct ctKey const* key)
{
	struct FragData* f = ctLookup(ft->ct, now, key);
	// if != NULL the reference-counter has been incremented
	if (f == NULL) {
		// Did not exist. Allocate it from the fragDataPool
		struct Item* i = itemAllocate(ft->fragDataPool);
		if (i == NULL)
			return NULL;
		f = (struct FragData*) i->data;
		f->referenceCounter = 1; /* Only the CT refer the object */
		f->firstFragmentSeen = 0;
		f->storedFragments = NULL;

		switch (ctInsert(ft->ct, now, key, f)) {
		case 0:
			// Make it look like a succesful ctLookup()
			REFINC(f->referenceCounter);
			break;
		case 1:
			/*
			  Another thread has also allocated the entry and we lost
			  the race. Yeld, and use the inserted object.
			 */
			fragDataUnlock(ft, f); /* will release our allocated object */
			f = ctLookup(ft->ct, now, key);
			if (f == NULL) {
				/*
				  The object created by another thread has been
				  deleted again! This should not happen. Give up.
				 */
				return NULL;
			}
			break;
		default:
			/*
			  Failed to allocate a bucket in the ct. Our FragData is
			  locked with referenceCounter=1, call fragDataUnlock() to
			  release it.
			 */
			fragDataUnlock(ft, f);
			return NULL;
		}
	}
	return f;
}



/*
  bucketPoolAllocate() and bucketPoolFree() are passed in ctCreate().
  When a hash collision occurs in the ct it must allocate a new hash
  bucket. This *only* occurs is several packets gets the same fragment
  hash. This should normally be extremly rare and if it happens it
  could be a DoS attack. Because of that we can't just do "malloc" so
  a "bucket-pool" is used.
 */
static void* bucketPoolAllocate(void* user_ref)
{
	struct FragTable* ft = user_ref;
	struct Item* i = itemAllocate(ft->bucketPool);
	if (i == NULL)
		return NULL;
	CNTINC(ft->fstats->bucketsAllocated);
	CNTINC(ft->fstats->bucketsUsed);
	return i->data;
}
static void bucketPoolFree(void* user_ref, void* b)
{
	struct Item* item = ITEM_OF(b);
	itemFree(item);
	struct FragTable* ft = user_ref;
	CNTDEC(ft->fstats->bucketsUsed);
}

/* ----------------------------------------------------------------------
*/


static void initMutex(struct Item* item)
{
	struct FragData* f = (struct FragData*)(item->data);
	MUTEX_INIT(&f->mutex);
}

struct FragTable* fragInit(
	unsigned hsize,
	unsigned maxBuckets,
	unsigned maxFragments,
	unsigned mtu,
	unsigned timeoutMillis)
{
	struct FragTable* ft = calloc(1, sizeof(*ft));
	if (ft == NULL)
		return NULL;
	ft->bucketPool = itemPoolCreate(maxBuckets, sizeof_bucket, NULL);
	ft->fragmentPool = itemPoolCreate(maxFragments, mtu, NULL);
	// In theory we can have max (hsize + maxBuckets) FragData objects in the ct
	ft->fragDataPool = itemPoolCreate(
		hsize + maxBuckets, sizeof(struct FragData), initMutex);
	// Init stats
	ft->fstats = &ft->_fstats;
	ft->fstats->bucketsMax = maxBuckets;
	ft->fstats->fragsMax = maxFragments;
	ft->fstats->mtu = mtu;
	/* A pointer to the FragTable structure is passed as "user_ref" to
	   ctCreate() and is passed back as the firsts parameter in call-backs. */
	ft->ct = ctCreate(
		hsize, timeoutMillis * MS, fragDataUnlock, fragDataLock,
		bucketPoolAllocate, bucketPoolFree, ft);
	assert(ft->ct != NULL);
	return ft;
}
void fragUseStats(struct FragTable* ft, struct fragStats* stats)
{
	*stats = *ft->fstats;
	ft->fstats = stats;
	ctUseStats(ft->ct, &stats->ctstats);
}

int fragInsertFirst(
	struct FragTable* ft, struct timespec* now,
	struct ctKey* key, unsigned hash)
{
	struct FragData* f = fragDataLookup(ft, now, key);
	if (f == NULL) {
		return -1;				/* Out of buckets */
	}
	// Lock here to avoid a race with fragGetHashOrStore()
	LOCK(&f->mutex);
	f->hash = hash;
	f->firstFragmentSeen = 1;
	UNLOCK(&f->mutex);
	fragDataUnlock(NULL, f);
	return 0;					/* OK return */
}

struct Item* fragGetStored(
	struct FragTable* ft, struct timespec* now, struct ctKey* key)
{
	struct FragData* f = ctLookup(ft->ct, now, key);
	if (f == NULL)
		return NULL;

	struct Item* storedFragments;
	LOCK(&f->mutex);
	storedFragments = f->storedFragments;
	f->storedFragments = NULL;
	UNLOCK(&f->mutex);

	fragDataUnlock(NULL, f);
	return storedFragments;
}

int fragGetHash(
	struct FragTable* ft, struct timespec* now,
	struct ctKey* key, unsigned* hash)
{
	struct FragData* f = ctLookup(ft->ct, now, key);
	if (f == NULL || !f->firstFragmentSeen) {
		return -1;
	}
	*hash = f->hash;
	fragDataUnlock(NULL, f);
	return 0;
}

int fragGetHashOrStore(
	struct FragTable* ft, struct timespec* now,
	struct ctKey* key, unsigned* hash,
	void const* data, unsigned len)
{
	struct FragData* f = fragDataLookup(ft, now, key);
	if (f == NULL) {
		return -1;				/* Out of buckets */
	}
	if (f->firstFragmentSeen) {
		*hash = f->hash;
		fragDataUnlock(NULL, f);
		return 0;				/* OK return */
	}

	/*
	  We have not seen the first fragment. Store this fragment.
	 */
	struct ItemPoolStats const* stats = itemPoolStats(ft->fragmentPool);
	if (len > stats->itemSize) {
		fragDataUnlock(NULL, f);
		return -1;				/* Fragment > MTU ?? */
	}

	struct Item* item = itemAllocate(ft->fragmentPool);
	if (item == NULL) {
		fragDataUnlock(NULL, f);
		return -1;				/* Out of fragment space */
	}

	item->len = len;
	memcpy(item->data, data, len);

	int rc;
	LOCK(&f->mutex);
	if (f->firstFragmentSeen) {
		/*
		 The first-fragment has arrived in another thread while we
		 were working.  This race should be rare. Do NOT keep the
		 mutex for longer than needed.
		*/
		rc = 0;
	} else {
		item->next = f->storedFragments;
		f->storedFragments = item;
		CNTINC(ft->fstats->fragsAllocated);
		rc = 1;
	}
	UNLOCK(&f->mutex);

	if (rc == 0) {
		*hash = f->hash;
		// Release no-longer-needed Item outside the lock
		itemFree(item);
	}

	fragDataUnlock(NULL, f);
	return rc;
}

void fragGetStats(
	struct FragTable* ft, struct timespec* now, struct fragStats* stats)
{
	/*
	  To call ctStats() will trig a full GC. I.e. call-backs to
	  fragDataUnlock for timed-out FragData objects.
	 */
	struct ctStats const* ctstats = ctStats(ft->ct, now);
	if (stats != ft->fstats) {
		*stats = *ft->fstats;
		stats->ctstats = *ctstats;
	}

#ifdef SANITY_CHECK

	struct ItemPoolStats const* istats;

	istats = itemPoolStats(ft->bucketPool);
	assert(ft->fstats->bucketsMax == istats->size);
	assert(ft->fstats->ctstats.collisions == (istats->size - istats->nFree));

	istats = itemPoolStats(ft->fragmentPool);
	assert(ft->fstats->fragsMax == istats->size);
	assert(ft->fstats->mtu == istats->itemSize);

	istats = itemPoolStats(ft->fragDataPool);
	assert(stats->ctstats.active == (istats->size - istats->nFree));
	assert(istats->size == (ft->fstats->bucketsMax + stats->ctstats.size));

#endif
}

void fragPrintStats(struct fragStats* sft)
{
	printf(
		"{\n"
		"  \"hsize\":            %u,\n"
		"  \"ttlMillis\":        %u,\n"
		"  \"collisions\":       %u,\n"
		"  \"inserts\":          %u,\n"
		"  \"rejected\":         %u,\n"
		"  \"lookups\":          %u,\n"
		"  \"objGC\":            %u,\n"
		"  \"mtu\":              %u,\n"
		"  \"bucketsMax\":       %u,\n"
		"  \"bucketsAllocated\": %u,\n"
		"  \"bucketsUsed\":      %u,\n"
		"  \"fragsMax\":         %u,\n"
		"  \"fragsAllocated\":   %u,\n"
		"  \"fragsDiscarded\":   %u\n"
		"}\n",
		sft->ctstats.size, (unsigned)(sft->ctstats.ttlNanos/1000000),
		sft->ctstats.collisions, sft->ctstats.inserts,
		sft->ctstats.rejectedInserts, sft->ctstats.lookups, sft->ctstats.objGC,
		sft->mtu, sft->bucketsMax, sft->bucketsAllocated, sft->bucketsUsed,
		sft->fragsMax, sft->fragsAllocated,
		sft->fragsDiscarded);
}

