# Nordix/nfqueue-loadbalancer - Fragment tracking

We must track fragments, for example to store the computed (L4) hash
from the first fragment, also to store fragments out of order. The
fragment/connection tracker is implemented as a hash table;

<img src="fragtrack.svg" alt="Fragment track table" width="80%" />

The key is the tuple `{source, dest, fragid}`. This is the same, and
unique, for all fragments of a packet. The FragData `state` can be;

* Valid - We have seen the first fragment
* Storing fragments - We have not seen the first fragment
* Poisoned - We have lost a fragment (or something worse)



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


## Reassembler

Packets are never reassembled by `nfqlb`. However, the book-keeping is
made so `nfqlb` knows when the whole packet has been processed and the
bucket can be released. The reassembler is disabled by default and
must be enabled with option;

```
nfqlb lb --reassembler=1000 ...
```
The example will enable reassembly of max 1000 packets simultaneously.

If fragments must be stored, that is if fragments are reordedred and
the first fragment doesn't arrive first, reassembly is disabled for
that packet.


## TTL and GC

If the reassembler can't be used for a packet the fragments times out.

Buckets have a Time-To-Live (ttl) and a time when they are last
refered (timeRefered). When

```
now - timeRefered > ttl
```

the bucket is "stale". It's data is outdated and it may be re-used.
When a bucket is refered (insert or lookup) a check is made of the
bucket, and it's linked buckets, and any stale buckets are re-claimed. A
Garbage Collect (GC) on refer of sorts.

This means that over time stale allocated buckets may be accumulated
at places in the table.

<img src="fragtrack-stale-buckets.svg" alt="stale-buckets" width="80%" />

There is *no need* for an automatic (e.g. periodic) global GC!  The
laws of statistics and careful configuration is sufficient. This may
feel uncertain but fortunately it can be simulated.



## Configuration

The important parameters are;

* **ft_ttl** - Time to Live, but really *"maximum time between fragments of the same packet"*
* **ft_size** - The hash table size
* **ft_buckets** - Extra "ctBucket" on hash collisions


First we must decide a `ttl`. Since fragments of the same packets are
normally (always?) sent as a burst from the source, the ttl can be set
fairly low. In this example we set `ttl=200ms`. This is the time in
Linux for the first re-transmit of TCP packets so at least someone
thinks packets should not take more time.

Then we must decide a continuous rate of fragmented packets that we
must handle (*packets* that is, not fragments). This metric is
probably not available so a rough estimate must do. We pick
`rate=10000pkt/S` in this example.

If hashing was perfect this would give a table size of;
```
ft_size = rate * ft_ttl
hsize = 10000 * 0.2 = 2000 in our example.
```

But we will get collisions so we must have the pool for extra buckets
and use a larger table. The recommended formula is;

```
ft_size = rate * ft_ttl * C
ft_buckets = ft_size
```

And as always with hash tables, *the size should be a prime*.

A good value of `C` can be found with simulations;
```
make -C src clean; make -j8 -C src CFLAGS=-D=UNIT_TEST test_progs
alias ct=/tmp/$USER/nfqlb/lib/test/conntrack-test
ct -h
ct --repeat=1 --duration=300 --rate=10000 --ft_ttl=200 --ft_size=1999 --ft_buckets=1999
{
  "ttlMillis":     200,
  "size":          1999,
  "active":        1999,
  "collisions":    1580599,
  "inserts":       2998902,
  "rejected":      0,
  "lookups":       0,
  "objGC":         2996903,
  "bucketsMax":    1999,
  "bucketsPeak":   1947,
  "bucketsStale":  1649,
  "percentLoss":   0.0
}
```

Here we run a simulation of 10000pkt/S rate for a simulated time of
300s (5m). With C=1 we get no packet loss. A reasonable value may be
C=2. The simulation itself takes less than 1s.

Remember that if the reassembler is used only fragments out of order
will be subject to timeouts.
