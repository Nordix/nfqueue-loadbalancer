# Nfqlb - Manual test of functions

Perform manual test of nfqlb function such as fragment handling.  For
understanding of nfqlb and the function test environment this is *very*
useful.


## Simple fragment handling

The simplest case is when all fragments arrives to the same LB and in
order. The first packet (containing ports) is stored in the
[https://github.com/Nordix/nfqueue-loadbalancer/blob/master/fragtrack.md](
connection tracker). When subsequent fragments arrive a lookup is made
in the connection table.

```
__nrouters=1 ./nfqlb.sh test start > $log
# On vm-201 (load-balancer)
tcpdump -ni eth2 icmp6
# On vm-201
nfqlb show
nfqlb stats
# On vm-221 (tester)
ping -c1 -W1 -s 20000 1000::
# Back on vm-201
nfqlb stats
```

You can trace the fragments with `tcpdump`. The stats should be empty
before the test, and show some inserts/lookups after:

```
# Before:
{
  "hsize":            500,
  "ttlMillis":        200,
  "collisions":       0,
  "inserts":          0,
  "rejected":         0,
  "lookups":          0,
  "objGC":            0,
  "mtu":              1500,
  "bucketsMax":       500,
  "bucketsAllocated": 0,
  "bucketsUsed":      0,
  "fragsMax":         100,
  "fragsAllocated":   0,
  "fragsDiscarded":   0
  "reAssembled":      0
}
# After:
{
  "hsize":            500,
  "ttlMillis":        200,
  "collisions":       0,
  "inserts":          1,
  "rejected":         0,
  "lookups":          14,
  "objGC":            0,
  "mtu":              1500,
  "bucketsMax":       500,
  "bucketsAllocated": 0,
  "bucketsUsed":      0,
  "fragsMax":         100,
  "fragsAllocated":   0,
  "fragsDiscarded":   0
  "reAssembled":      0
}
```



## Fragments out-of-order

When fragments arrives out of order, [packets must be stored](
https://github.com/Nordix/nfqueue-loadbalancer/blob/master/fragments.md)
until the first packet arrives. The [evil-tester](
https://github.com/Nordix/xcluster/tree/master/ovl/network-topology#evil-tester)
can re-order fragments for testing when the `--fragrev` option is used.

```
__nrouters=1 ./nfqlb.sh test --fragrev start > $log
# On vm-201 (load-balancer)
nfqlb stats
# On vm-221 (tester)
ping -c1 -W1 -s 20000 1000::
# Back on vm-201
nfqlb stats

# After
{
  "hsize":            500,
  "ttlMillis":        200,
  "collisions":       0,
  "inserts":          1,
  "rejected":         0,
  "lookups":          27,
  "objGC":            0,
  "mtu":              1500,
  "bucketsMax":       500,
  "bucketsAllocated": 0,
  "bucketsUsed":      0,
  "fragsMax":         100,
  "fragsAllocated":   13,
  "fragsDiscarded":   0
  "reAssembled":      0
}
```

You can see that 13 fragments has been allocated and later injected
via the tap interface `nfqlb0`. You may check that with tcpdump:

```
# On vm-002
tcpdump -ni nfqlb0
```



## UDP with fragmentation

There is a [proposed formula](
https://github.com/Nordix/nfqueue-loadbalancer/blob/master/fragtrack.md#configuration)
for computing the size of the frag-table and the number of "spare
buckets" required for a certain packet rate. In our default setup we have;

```
ft_size = 500
ft_ttl = 0.2 S
ft_buckets = 100
# Formula: ft_size = rate * ft_ttl * C
rate = 500 / 0.2 = 2500 pkt/S  # (with C=1)
```

We can make simulation with these values using the nfqlb
unit-tests. **NOTE** that the ft_size *must* be a prime in unit-test,
so we use 499:

```
cd src
make clean; CFLAGS="-Werror -DUNIT_TEST" make -j$(nproc) test_progs
cp /tmp/$USER/nfqlb/lib/test/conntrack-test /tmp
alias ct=/tmp/conntrack-test
ct --ft_size=499 --ft_buckets=100 --ft_ttl=200 --rate=2500 --duration=20 --repeat=1
{
  "ttlMillis":     200,
  "size":          499,
  "active":        376,
  "collisions":    21196,
  "inserts":       49996,
  "rejected":      11703,
  "lookups":       0,
  "objGC":         37917,
  "bucketsMax":    100,
  "bucketsPeak":   100,
  "bucketsStale":  221,
  "percentLoss":   23.4
}
```

Well, that didn't work (~23% packet loss). Seems that the formula is a
bit optimistic, or the simulation is too pessimistic. With C=2 we get a
rate of 1250 pkt/S which is reasonable (even though there is a small
packet loss in simulations).

Now let's test that in the function-test environment:

```
__nrouters=1 ./nfqlb.sh test start > $log
# On vm-221 (tester)
ctraffic -monitor -psize 2048 -rate 2500 -nconn 40 -timeout 20s -udp -address [1000::]:5003 | jq
{
  "hsize":            500,
  "ttlMillis":        200,
  "collisions":       9861,
  "inserts":          24999,
  "rejected":         0,
  "lookups":          49998,
  "objGC":            24262,
  "mtu":              1500,
  "bucketsMax":       500,
  "bucketsAllocated": 8342,
  "bucketsUsed":      237,
  "fragsMax":         100,
  "fragsAllocated":   0,
  "fragsDiscarded":   0
  "reAssembled":      0
}
```

**NOTE** that the rate is in kb/S and our packets are 2k, so to sent
1250 pkt/S you must use `-rate 2500`.

The simulation in unit-test is harder on the table than real
traffic. In function-test we can actually come close to the 2500 pkt/S
as the formula gave us for C=1. But with 2500 pkt/S we get some packet loss:

```
__nrouters=1 ./nfqlb.sh test start > $log
# On vm-221 (tester)
ctraffic -monitor -psize 2048 -rate 5000 -nconn 40 -timeout 20s -udp -address [1000::]:5003 | jq
{
  "hsize":            500,
  "ttlMillis":        200,
  "collisions":       22390,
  "inserts":          39872,
  "rejected":         20,
  "lookups":          80024,
  "objGC":            38925,
  "mtu":              1500,
  "bucketsMax":       500,
  "bucketsAllocated": 18005,
  "bucketsUsed":      427,
  "fragsMax":         100,
  "fragsAllocated":   2,
  "fragsDiscarded":   2
  "reAssembled":      0
}
```

The packet loss was ~0.4%. "ft_buckets=100" is lower than the
recommended 500, so the formula seems pretty OK, but the simulation is
pessimistic.


### The reassmbler

The [reassebler](
https://github.com/Nordix/nfqueue-loadbalancer/blob/master/fragtrack.md#reassembler)
doesn't reassemle packets, but it is used to release fragments in the
frag-table before the ttl has expired, and thus allow higher rates. It
came as a requirement for high load fragmented UDP traffic. It has not
been tested on real HW, but we can test it in the function-test
environment. We repeat the last test but with a *much* higher rate and
the reassembler.

```
export xcluster_LBOPT="--queue=0:3 --reassembler=1000"
__nrouters=1 ./nfqlb.sh test start > $log
# On vm-221 (tester)
ctraffic -monitor -psize 2048 -rate 12000 -nconn 40 -timeout 20s -udp -address [1000::]:5003 | jq
{
  "hsize":            500,
  "ttlMillis":        200,
  "collisions":       20,
  "inserts":          119996,
  "rejected":         0,
  "lookups":          239992,
  "objGC":            0,
  "mtu":              1500,
  "bucketsMax":       500,
  "bucketsAllocated": 20,
  "bucketsUsed":      0,
  "fragsMax":         100,
  "fragsAllocated":   0,
  "fragsDiscarded":   0
  "reAssembled":      119996
}
```

As you can see the reassembler makes a **big** impact.



## Load-balancer tier

If you have a load-balancer tier and fragments can arrive to different
load-balancers you must [collect all fragmens on one load-balancer](
https://github.com/Nordix/nfqueue-loadbalancer/blob/master/fragments.md).
The function is not explicitly tested, but it is almost always
configured in function tests so it's used a lot.

I have no simple way to force Linux to use random targets for ECMP,
but usually the receiving LB is not the one that should make the
tracking, so fragments are fowarded to another LB. The function can be
examined with `tcpdump`, but in this example we use the `nfqlb` trace.


```
__nrouters=3 ./nfqlb.sh test start > $log
# On a router:
nfqlb show --shm=nfqlbLb
Shm: nfqlbLb
  Fw: own=203
  Maglev: M=397, N=10
   Lookup: 2 1 1 1 1 0 0 1 1 0 1 0 1 1 2 0 1 0 2 1 0 0 0 2 0...
   Active: 201(0) 202(1) 203(2)
# On all routes
nfqlb trace --selection=frag
# On vm-221 (tester)
ping -c1 -W1 -s 8000 -I 1000::1:20.0.0.0 1000::
# Try differnt source addresses
# On an LB that can't handle fragments locally you should see
Fragment to LB tier. fw=202
# And on the LB that shall handle the fragments:
First fragment
Handle non-first frag locally fwmark=103
```




## Path MTU discovery

The [PMTU discovery function](
https://github.com/Nordix/nfqueue-loadbalancer/blob/master/destunreach.md)
is tested with a http request with data >>mtu, and the [squeeze
chain](https://github.com/Nordix/xcluster/tree/master/ovl/mtu#squeeze-chain).

```
__nrouters=1 ./nfqlb.sh test start_mtu_squeeze > $log
# On a VM
tracepath 1000::1:20.0.0.0
# On vm-221 (tester)
curl http://[1000::] -s -m2 --interface 1000::1:20.0.0.1
# This has a 1/$__nvm chance of success without the PMTU discovery function
# so try with different source addresses

# Packet trace:
# Restart! (to clear any mtu caches)
__nrouters=1 ./nfqlb.sh test start_mtu_squeeze > $log
# On the host
xc tcpdump --start 201 eth1
# On vm-221
curl http://[1000::] -s -m2 --interface 1000::1:20.0.0.1
# Back on the host
xc tcpdump --get 201 eth1
wireshark /tmp/vm-201-eth1.pcap &
```

