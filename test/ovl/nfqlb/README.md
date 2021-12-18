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
./nfqlb.sh test basic > $log

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
xcluster_LBOPT="--queue=0:3 --reassembler=1000" ./nfqlb.sh test --verbose udp > $log
```

### PMTU discovery test

There is a general problem with
[dest-unreachable](../../../destunreach.md) packets and load-balancing
that is hadled by `nfqlb`. The [ovl/mtu squeeze chain](https://github.com/Nordix/xcluster/tree/master/ovl/mtu#squeeze-chain) is used for test.

```
./nfqlb.sh test mtu > $log
```

Manual test;
```
__nrouters=1 ./nfqlb.sh test --no-stop mtu > $log
# On vm-001
tracepath 20.0.0.0
# On vm-201
tcpdump -ni eth2 icmp6
# On vm-221
curl --interface 1000::1:20.0.0.2 http://[1000::]
```

### SCTP test

```
./nfqlb.sh test sctp > $log
```

Manual test with dual-path;
```
./nfqlb.sh test start_dual_path > $log
# On vm-221;
sctpt client --log=6 --addr=10.0.0.0,1000:: --laddr=192.168.2.221,1000::1:192.168.6.221
# On vm-201/vm-202
tcpdump -ni eth1 sctp
iptables -A FORWARD -p sctp -j DROP
iptables -D FORWARD 1
# On vm-221
sctpt stats clear
watch sctpt stats
# On vm-221 in another terminal
sctpt ctraffic --log=6 --addr=10.0.0.0,1000:: --laddr=192.168.2.221,1000::1:192.168.6.221 --clients=8 --rate=10
```

### Manual reject tests

The options to configure a mark to be set instead of just drop;
```
  --notargets_fwmark= Set when there are no targets 
  --nolb_fwmark= Set when there is no matching LB 
```

has no automatic test case. It must currently be tested
manually. Reject rules for fwmark 1 and 2 are inserted already.

Without flows;
```
export xcluster_LBOPT="--notargets_fwmark=1"
__nrouters=1 ./nfqlb.sh test start > $log
# On vm-201
nfqlb deactivate 101 102 103 104
# On vm-221
telnet 10.0.0.0 5001
telnet 1000:: 5001
# Should get "Connection refused", not hang
```

With flows;
```
export xcluster_LBOPT="--notargets_fwmark=1 --nolb_fwmark=2"
export xcluster_FLOW=yes
__nrouters=1 ./nfqlb.sh test start > $log
# On vm-201
nfqlb deactivate 101 102 103 104
# On vm-221
telnet 10.0.0.0 5001
telnet 1000:: 5001
# Should get "Connection refused", not hang
# On vm-201
nfqlb flow-delete --name=default
# On vm-221
telnet 10.0.0.0 5001
telnet 1000:: 5001
# Should get "No route to host"
```

## Test the HW setup

To prepare for test on real HW we test the
[setup](https://github.com/Nordix/nfqueue-loadbalancer/blob/master/test/README.md#fragmentation-test)
on `xcluster`.

```
vip=10.0.0.0
#vip=fd01::2000
./nfqlb.sh test --vip=$vip start_hw_setup > $log
# On vm-201
echo $vip    # (should be the same as above)
export __sudo=env
export __lbopts="--ft_size=10000 --ft_buckets=10000 --ft_frag=100 --ft_ttl=50"
nfqlb_performance.sh dsr_test --vip=$vip -P4
nfqlb_performance.sh dsr_test --direct --vip=$vip -P4
nfqlb_performance.sh dsr_test --vip=$vip -P8 -u -b50M -l 2400
# On vm-001
killall iperf
echo $vip | grep -q : && opt=-V
/root/Downloads/iperf -s $opt -B $vip --udp
```

