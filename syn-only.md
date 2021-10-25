# Nordix/nfqueue-loadbalancer - Improved TCP performance

For TCP it is not necessary to redirect *all* packets to
user-space. Only the `SYN` packets may be redirected and then we let
the "conntracker" take care of subsequent packets in the kernel.

This will not only boost performance for TCP it will also preserve all
existing connections when the clients are scaled since the conntracker
is stateful.


The iptables setup is showed below, and the counters shows the
improvement;

```
Chain PREROUTING (policy ACCEPT 3653 packets, 3334K bytes)
 pkts bytes target     prot opt in     out     source               destination         
 6498 3540K VIP        all      eth2   *       ::/0                 1000::/112          

Chain POSTROUTING (policy ACCEPT 10124 packets, 6873K bytes)
 pkts bytes target     prot opt in     out     source               destination         
 6498 3540K VIPOUT     all      *      *       ::/0                 1000::/112          

Chain ESTABLISHED (1 references)
 pkts bytes target     prot opt in     out     source               destination         
 6398 3532K CONNMARK   all      *      *       ::/0                 ::/0                 CONNMARK restore
 6398 3532K ACCEPT     all      *      *       ::/0                 ::/0                

Chain VIP (1 references)
 pkts bytes target     prot opt in     out     source               destination         
 6398 3532K ESTABLISHED  all      *      *       ::/0                 ::/0                 ctstate ESTABLISHED
  100  8000 NFQUEUE    all      *      *       ::/0                 ::/0                 NFQUEUE num 2

Chain VIPOUT (1 references)
 pkts bytes target     prot opt in     out     source               destination         
  100  8000 CONNMARK   all      *      *       ::/0                 ::/0                 ctstate NEW CONNMARK save
```

Note that only 100 packets (the SYNs) are directed to user-space.

When the lb's are scaled we may redirect all "INVALID" packets to
user-space. Basically this will be as using nfqueue without the SYN
optimization, it will work but slower. It should also be possible to
use sysctls to let other lb's "pickup" the connection;

```
nf_conntrack_tcp_be_liberal - BOOLEAN
	- 0 - disabled (default)
	- not 0 - enabled

	Be conservative in what you do, be liberal in what you accept from others.
	If it's non-zero, we mark only out of window RST segments as INVALID.

nf_conntrack_tcp_loose - BOOLEAN
	- 0 - disabled
	- not 0 - enabled (default)

	If it is set to zero, we disable picking up already established
	connections.
```

## The problems

For this to work incoming and return traffic must pass through the
same load-balancer. This basically rules out Direct Server Return
(DSR).

When DSR is not used we must do DNAT. When we do that the Linux kernel
must reassemble packets
([*](https://unix.stackexchange.com/questions/650790/unwanted-defragmentation-of-forwarded-ipv4-packets))
so `nfqlb` can't do [fragment handling](fragments.md). However the
normal case is that fragments do arrive to the same load-balancer so
this may be ok.