# github.com/Nordix/nfqueue-loadbalancer - ovl/nfqlb

Function tests for the
[nfqueue-loadbalancer](https://github.com/Nordix/nfqueue-loadbalancer).

These tests are using the
[evil-tester](https://github.com/Nordix/xcluster/tree/master/ovl/network-topology#evil-tester)
network topology. The "evil-tester" can for instance be used to
re-order fragments.

## Usage

Basic tests;
```
test -n "$log" || log=/tmp/$USER/xcluster-test.log
# Fast TCP/UDP/ping test with ipv4 and ipv6
./nfqlb.sh test > $log

# For experiments, just start and use only one load-balancer;
__nrouters=1 ./nfqlb.sh test start > $log
vm 201
# On vm-201 (the load-balancer);
nfqlb show
# On vm 221 (tester)
ping -c1 -W1 -s 20000 1000::
# This packet must be fragmented, check the frag stats on vm-201
nfqlb stats
# You can also use "tcpdump" to check the traffic
```

While the most basic tests can be performed with user-space networking
you *must* setup `xcluster` in a
[netns](https://github.com/Nordix/xcluster/blob/master/doc/netns.md)
for more demanding tests.


### UDP test with fragmentation

```
./nfqlb.sh test --verbose udp > $log
# Fragments are reversed
./nfqlb.sh test --verbose --fragrev udp > $log
./nfqlb.sh test --verbose --fragrev --vip=10.0.0.0:5001 udp > $log
# Multi queue;
xcluster_LBOPT="--queue=0:3" ./nfqlb.sh test --verbose --fragrev udp > $log
```
