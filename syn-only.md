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

When the lb's are scaled we must redirect all "INVALID" packets to
user-space. Basically this will be as using nfqueue without the SYN
optimization, it will work but slower.



## The problems

For this to work incoming and return traffic must pass through the
same load-balancer. This basically rules out Direct Server Return
(DSR).

A solution *may* be to check "SYN_SEEN" in the conntracker instead of
"ESTABLISHED" but that require a custom iptables module.

When DSR is not used we must do DNAT. When we do that the Linux kernel
will reassemble packets
([*](https://unix.stackexchange.com/questions/650790/unwanted-defragmentation-of-forwarded-ipv4-packets))
so `nfqlb` can't do [fragment handling](fragments.md). However the
normal case is that fragments do arrive to the same load-balancer so
this may be ok in some installations.