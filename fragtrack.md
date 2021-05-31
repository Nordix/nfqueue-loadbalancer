# Nordix/nfqueue-loadbalancer - Fragment tracking

We must track fragments, for example to store the computed (L4) hash
from the first fragment, also to store fragments out of order. The
fragment/connection tracker is implemented as a hash table;

<img src="fragtrack.svg" alt="Fragment track table" width="80%" />

The key is the tuple `{source, dest, fragid}`. This is the same, and
unique, for all fragments of a packet.

### Pools

All dynamically allocated objects, extra ctBuckets, FragData and
stored fragments, are taken from pre-allocated fixed size pools. This
prevents memory fragmentation caused by frequent malloc/free and puts
a limit on used resources.

### Locks

The entire table is *never* locked. Buckets are locked individually
and temporary when refered. The Bucket is *not* locked after return of
a `lookup()` operation. This means that the bucket may be freed when
the `FragData` is in use. To cope with this the `FragData` contains a
reference counter.

The mutex in `FragData` is used to protect the variables and is held
for very short times.

## TTL and GC

The prefered operation of `nfqlb` is to *never* explicitly remove
fragments from the table. All fragment times out.

Buckets have a Time-To-Live (ttl) and a time when they are last
refered (timeRefered). When

```
now - timeRefered > ttl
```

the bucket is "stale". It's data is outdated and it may be re-used.
When a bucket is refered (insert or lookup) a check is made of the
bucket, and it's linked buckets, and any stale bucket are re-claimed. A
Garbace Collect (GC) on refer of sorts.

This means that over time stale allocated buckets will be accumulated
at places in the table.

<img src="fragtrack-stale-buckets.svg" alt="stale-buckets" width="80%" />

There is *no need* for an automatic (e.g. periodic) global GC!  The
laws of statistics and careful configuration is sufficient. This may
feel uncertain but fortunately it can be simulated.



## Configuration

The important parameters are `ttl` (ft_ttl), the table size (ft_size)
and the size of the pool for extra "ctBucket" on hash collisions
(ft_buckets).

First we must decide a `ttl`. Ttl is really *"maximum time between
fragments of the same packet"*. Since fragments of the same packets
are normally (always?) sent as a burst from the source the ttl can be
set fairly low. In this example we set `ttl=200ms`. This is the time
in Linux for the first re-transmit of TCP packets so at least someone
thinks packets should not take more time.

Then we must estimate a continuous rate of fragmented packets (packets
that is, not fragments) that we must handle. This is not possible in
most cases I think so the rate is taken out of thin air, we pick
`rate=10000pkt/S` in this example.
