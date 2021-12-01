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
#include <assert.h>
#else
#define assert(x)
#endif

#define MS 1000000				/* One milli second in nanos */

#define REFINC(x) __atomic_add_fetch(&(x),1,__ATOMIC_SEQ_CST)
#define REFDEC(x) __atomic_sub_fetch(&(x),1,__ATOMIC_SEQ_CST)
#define CNTINC(x) __atomic_add_fetch(&(x),1,__ATOMIC_RELAXED)
#define CNTDEC(x) __atomic_sub_fetch(&(x),1,__ATOMIC_RELAXED)
#define ATOMIC_LOAD(x) __atomic_load_n(&(x),__ATOMIC_RELAXED)
#define ATOMIC_STORE(x,v) __atomic_store_n(&(x),v,__ATOMIC_RELAXED)
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
	struct FragReassembler* reassembler;
};

/*
  FragData objects are stored in the ct. Since these are returned in a
  ctLookup() we must ensure that the object is not released by another
  thread while the ctLookup() caller is using it. So, a
  reference-counter is used.
*/
enum FragDataState {
	FragData_hashValid,
	FragData_storingFragments,
	FragData_poisoned
};
struct FragData {
	int referenceCounter;
	MUTEX(mutex);
	enum FragDataState state;
	int value;
	struct Item* storedFragments;
	void* assemblyData;
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
	int refCount = REFDEC(f->referenceCounter);
	assert(refCount >= 0);
	if (refCount == 0) {
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
		if (ft->reassembler != NULL && f->assemblyData != NULL) {
			ft->reassembler->destroy(f->assemblyData);
		}
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
		f->state = FragData_storingFragments;
		f->storedFragments = NULL;
		if (ft->reassembler != NULL)
			f->assemblyData = ft->reassembler->new();

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

struct FragTable* fragTableCreate(
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

// https://stackoverflow.com/questions/5617925/maximum-values-for-time-t-struct-timespec/
// Assuming time_t is an integer, not a float
#include <limits.h>
#define MAXTIME ((((time_t) 1 << (sizeof(time_t) * CHAR_BIT - 2)) - 1) * 2 + 1)

void fragTableDestroy(struct FragTable* ft)
{
	struct timespec now;
	now.tv_sec = MAXTIME;
	struct fragStats stats;
	//printf("now.tv_sec = %ld\n", now.tv_sec);
	fragGetStats(ft, &now, &stats);
#ifdef SANITY_CHECK
	assert(stats.ctstats.active == 0);
	struct ItemPoolStats const* istat;
	istat = itemPoolStats(ft->fragDataPool);
	assert(istat->nFree == istat->size);
	istat = itemPoolStats(ft->fragmentPool);
	assert(istat->nFree == istat->size);
	istat = itemPoolStats(ft->bucketPool);
	assert(istat->nFree == istat->size);
#endif
	ctDestroy(ft->ct);
	itemPoolDestroy(ft->fragDataPool, NULL);
	itemPoolDestroy(ft->fragmentPool, NULL);
	itemPoolDestroy(ft->bucketPool, NULL);
	free(ft);
}

void fragRegisterFragReassembler(
	struct FragTable* ft, struct FragReassembler* reassembler)
{
	ft->reassembler = reassembler;
}

void fragUseStats(struct FragTable* ft, struct fragStats* stats)
{
	*stats = *ft->fstats;
	ft->fstats = stats;
	ctUseStats(ft->ct, &stats->ctstats);
}

int fragInsertFirst(
	struct FragTable* ft, struct timespec* now,
	struct ctKey* key, int value, struct Item** storedFragments,
	void const* data, unsigned len)
{
	struct FragData* f = fragDataLookup(ft, now, key);
	if (f == NULL) {
		if (storedFragments != NULL)
			*storedFragments = NULL;
		return -1;				/* Out of buckets */
	}
	// Lock here to avoid a race with fragGetHashOrStore()
	struct Item* storedFrags;
	LOCK(&f->mutex);
	if (ft->reassembler != NULL && f->assemblyData != NULL) {
		(void)ft->reassembler->handleFragment(f->assemblyData, data, len);
	}

	if (f->state != FragData_storingFragments) {
		/* This entry is poisoned (or we have got multiple first
		 * fragments) */
		UNLOCK(&f->mutex);
		if (storedFragments != NULL)
			*storedFragments = NULL;
		fragDataUnlock(ft, f);
		return -1;
	}
	f->value = value;
	ATOMIC_STORE(f->state, FragData_hashValid);
	storedFrags = f->storedFragments;
	f->storedFragments = NULL;
	UNLOCK(&f->mutex);
	fragDataUnlock(ft, f);

	if (storedFragments != NULL) {
		*storedFragments = storedFrags;
	} else {
		for (struct Item* i = storedFrags; i != NULL; i = i->next)
			CNTINC(ft->fstats->fragsDiscarded);
		itemFree(storedFrags);
	}

	return 0;					/* OK return */
}

int fragGetValue(
	struct FragTable* ft, struct timespec* now,
	struct ctKey* key, int* value)
{
	struct FragData* f = ctLookup(ft->ct, now, key);
	if (f == NULL)
		return -1;
	if (ATOMIC_LOAD(f->state) == FragData_hashValid) {
		*value = f->value;
		fragDataUnlock(ft, f);
		return 0;
	}
	fragDataUnlock(ft, f);
	return -1;
}

int fragGetValueOrStore(
	struct FragTable* ft, struct timespec* now,
	struct ctKey* key, int* value,
	void const* data, unsigned len)
{
	struct FragData* f = fragDataLookup(ft, now, key);
	if (f == NULL) {
		return -1;				/* Out of buckets */
	}

	if (ft->reassembler != NULL) {
		LOCK(&f->mutex);
		if (f->assemblyData != NULL) {
			if (ATOMIC_LOAD(f->state) == FragData_hashValid) {
				if (ft->reassembler->handleFragment(f->assemblyData, data, len) == 0) {
					ctRemove(ft->ct, now, key);
					CNTINC(ft->fstats->reAssembled);
				}
			} else {
				// It we havn't got the first fragment we must store
				// fragments and we can't use a reassembler.  Destroy
				// the assembler with the lock held.
				ft->reassembler->destroy(f->assemblyData);
				f->assemblyData = NULL;
			}
		}
		UNLOCK(&f->mutex);
	}

	/* No lock here! We will re-check with the lock later */
	switch (ATOMIC_LOAD(f->state)) {
	case FragData_hashValid:
		*value = f->value;
		fragDataUnlock(ft, f);
		return 0;				/* OK return (the normal case) */
	case FragData_poisoned:
		fragDataUnlock(ft, f);
		return -1;
	default:;
	}

	/*
	  We have not seen the first fragment. Store this fragment.
	 */
	struct ItemPoolStats const* stats = itemPoolStats(ft->fragmentPool);
	if (len > stats->itemSize) {
		fragDataUnlock(ft, f);
		return -1;				/* Fragment > MTU ?? Should not happen */
	}

	struct Item* item = itemAllocate(ft->fragmentPool);
	if (item == NULL) {
		/* We have lost a fragment. Poison the entry and discard any
		 * stored fragments. */
		struct Item* storedFrags;
		LOCK(&f->mutex);
		ATOMIC_STORE(f->state, FragData_poisoned);
		storedFrags = f->storedFragments;
		f->storedFragments = NULL;
		UNLOCK(&f->mutex);
		fragDataUnlock(ft, f);

		for (struct Item* i = storedFrags; i != NULL; i = i->next)
			CNTINC(ft->fstats->fragsDiscarded);
		itemFree(storedFrags);

		return -1;				/* Out of fragment space */
	}

	item->len = len;
	memcpy(item->data, data, len);

	int rc;
	LOCK(&f->mutex);
	/*
	  Re-check the state with the lock. Races should be very rare but
	  they *can* happen.
	 */
	/* We don't have to use ATOMIC_LOAD when we have the lock */
	switch (f->state) {
	case FragData_hashValid:
		/*
		  The first-fragment has arrived in another thread while we
		  were working.
		*/
		*value = f->value;
		rc = 0;
		break;
	case FragData_poisoned:
		/*
		  Something bad has happened in another thread while we were
		  working.
		*/
		rc = -1;
		break;
	default:
		/* Store the fragment */
		item->next = f->storedFragments;
		f->storedFragments = item;
		rc = 1;
	}
	UNLOCK(&f->mutex);

	if (rc != 1) {
		// Release no-longer-needed Item outside the lock
		itemFree(item);
	} else {
		CNTINC(ft->fstats->fragsAllocated);
	}

	fragDataUnlock(ft, f);
	return rc;
}

/* ----------------------------------------------------------------------
   Packet level functions;
 */

#include "iputils.h"

static void (*injectFragmentFn)(void const* data, unsigned len) = NULL;
void setInjectFn(void (*injectFn)(void const* data, unsigned len))
{
	injectFragmentFn = injectFn;
}
	
int handleFirstFragment(
	struct FragTable* ft, struct timespec* now,
	struct ctKey* key, int value,
	void const* data, unsigned len)
{
	struct Item* storedFragments;
	if (fragInsertFirst(
			ft, now, key, value, &storedFragments, data, len) != 0) {
		itemFree(storedFragments);
		return -1;
	}
	if (storedFragments != NULL) {
		if (injectFragmentFn != 0) {
			struct Item* i;
			for (i = storedFragments; i != NULL; i = i->next) {
				injectFragmentFn(i->data, i->len);
			}
		}
		itemFree(storedFragments);
	}
	return 0;
}

/* ----------------------------------------------------------------------
   Stats;
 */

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
		"  \"reAssembled\":      %u\n"
		"}\n",
		sft->ctstats.size, (unsigned)(sft->ctstats.ttlNanos/1000000),
		sft->ctstats.collisions, sft->ctstats.inserts,
		sft->ctstats.rejectedInserts, sft->ctstats.lookups, sft->ctstats.objGC,
		sft->mtu, sft->bucketsMax, sft->bucketsAllocated, sft->bucketsUsed,
		sft->fragsMax, sft->fragsAllocated,
		sft->fragsDiscarded, sft->reAssembled);
}

