#pragma once
/*
   SPDX-License-Identifier: Apache-2.0
   Copyright (c) 2021 Nordix Foundation
*/

#include "itempool.h"
#include "conntrack.h"

/*
  TCP uses PMTU to avoid fragmentation so fragmentation normally only
  happens for other protocols like UDP.

  About sizes

  It is of course impossible to give a definite answer, but there are
  some things to consider.

  The theoretical max of concurrent fragmented packets is (hsize +
  maxBuckets). But hashing is not perfect so a better estimate is
  (hsize * used% + maxBuckets).  The used% depends on the data and the
  quality of the hash function.

  The larger the hsize, the lower the probablility for collisions and
  therefore better performance. maxBuckets is only needed on hash
  collisions but they may happen even on low usage if we are unlucky.

  Since all packets times out there is a correlation between the
  fragmented packet rate (not the fragment rate) and the size and ttl.

    max-frag-packet-rate = max-packets / ttl-in-seconds

  With this we can make a rough estimate of that hsize is needed for a
  *sustained* rate of fragmented packets;

    hsize = rate * ttl * 2, maxBuckets = hsize

  For ttl=0.2 S, rate=1000 pkt/S we get hsize=200, maxBuckets=200

  Fragments out-of-order should be extremely rare. A conservative
  value for maxFragments may be ok. Even 0 (zero) if we don't care.

  MTU is the maximum size of stored fragments. It should be set to the
  MTU size for the ingress device.

  About timeout and GC

  The timeout should be set fairly low, e.g. 200ms. This is a
  fragmented packet we are talking about, not some re-send
  timeout. There is a standard saying 2sec I think, but don't care
  about that. Remember that we never exlpicitly remove anything from
  the fragment table, *everything* is removed by a timeout!

  Fragment entries that has timed out are not automatically
  freed. Instead they are GC'ed when a bucket is re-used. If there are
  no collisions it will always work with no overhead. We have
  optimized for the normal case.

  But if we have got collisions and allocated new bucket structures
  they will linger until next time we happen to hash to that same
  bucket (which may be never). This works well over time since buckets
  will most often be re-used eventually, and in case of high load,
  more frequently.

  However a full GC is trigged by reading the fragmentation stats. A
  reason may be for metrics or for an alarm on over-use of stored
  fragments which may indicate a DoS attack.

*/
struct FragTable;
struct FragTable* fragTableCreate(
	unsigned hsize,				/* Hash-table size */
	unsigned maxBuckets,		/* on top of hsize */
	unsigned maxFragments,		/* Max non-first fragments to store */
	unsigned mtu,				/* Max size of stored fragments */
	unsigned ttlMillis);		/* Timeout for fragments */

void fragTableDestroy(struct FragTable* ft);

struct FragReassembler {
	void* (*new)(void);
	// Returns;
	//  0 - Packet ready
	//  1 - More fragments needed
	// -1 - Packet is invalid
	int (*handleFragment)(void* r, void const* data, unsigned len);
	void (*destroy)(void* r);
};
void fragRegisterFragReassembler(
	struct FragTable* ft, struct FragReassembler* reassembler);


/*
  Inserts the first fragment and stores the passed hash to be used for
  sub-sequent fragments. The caller must call itemFree() on returned
  stored fragment Items. 
  return:
   0 - Hash stored
  -1 - Failed to store hash
*/
int fragInsertFirst(
	struct FragTable* ft, struct timespec* now,
	struct ctKey* key, unsigned hash, struct Item** storedFragments,
	void const* data, unsigned len);

/*
  Called for non-first fragments.
  return:
   0 - Hash is valid. Fragment is not stored.
   1 - Hash is NOT valid. The fragment is stored.
  -1 - Hash is NOT valid. Failed to store the fragment.
*/
int fragGetHashOrStore(
	struct FragTable* ft, struct timespec* now,
	struct ctKey* key, unsigned* hash,
	void const* data, unsigned len);

/*
  Called for non-first fragments when we don't want to store the fragment.
  Returns: hash
  return:
   0 - Hash is valid.
  -1 - Hash is NOT valid.
*/
int fragGetHash(
	struct FragTable* ft, struct timespec* now,
	struct ctKey* key, unsigned* hash);

/*
  Handle a IPv4 fragment.
  return:
   0 - Hash is valid.
   1 - Hash is NOT valid. The fragment is stored.
  -1 - Hash is NOT valid.
 */
int ipv4Fragment(
	struct FragTable* ft, struct timespec* now,
	void (*injectFn)(void const* data, unsigned len),
	void const* data, unsigned len, unsigned* hash);

/*
  Handle a IPv6 fragment.
  return:
   0 - Hash is valid.
   1 - Hash is NOT valid. The fragment is stored.
  -1 - Hash is NOT valid.
 */
int ipv6Fragment(
	struct FragTable* ft, struct timespec* now,
	void (*injectFn)(void const* data, unsigned len),
	void const* data, unsigned len, unsigned* hash);

struct fragStats {
	// Conntrack stats
	struct ctStats ctstats;
	// Frag stats
	unsigned mtu;
	unsigned bucketsMax;
	unsigned bucketsAllocated;
	unsigned bucketsUsed;
	unsigned fragsMax;
	unsigned fragsDiscarded;
	unsigned fragsAllocated;
	unsigned reAssembled;
};

void fragUseStats(struct FragTable* ft, struct fragStats* stats);
void fragGetStats(
	struct FragTable* ft, struct timespec* now, struct fragStats* stats);
void fragPrintStats(struct fragStats* sft);
