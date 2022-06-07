# Nordix/nfqueue-loadbalancer - Log and trace

Log and trace is supported but affects performance and should be used
with care.


## Log

Logs printouts are printed to `stderr`.

Log levels;

* `WARNING` (4) - Important events
* `NOTICE` (5) - Notable events (default)
* `INFO` (6) - Informative events
* `DEBUG` (7) - Debug events

If traffic is working there should not be any per-packet loggings even
on `debug` level.

The loglevel is stored in shared memory which make it possible to set
it in runtime with the `nfqlb loglevel` command;

```
# nfqlb loglevel
loglevel=5
# nfqlb loglevel 7
loglevel=7
# nfqlb loglevel
loglevel=7
```


## Trace

Trace is independent of the loglevel, that is trace is not loglevel>7.
Trace is based on a 32-bit "trace mask". Each bit represent a trace
area;

* `log` (0) - Log printouts are copied to trace printouts
* `packet` (1) - Per-packet printouts. Use with care!
* `frag` (2) - Fragment tracing. Per-fragment printouts
* `flow-conf` (3) - Flow configuration printouts
* `sctp` (4) - Per-packet sctp printouts
* `target` (5) - Trace flow target add/release (not per-packet)


To initiate trace you attach to a running `nfqlb` load-balancer with
`nfqlb trace`. Printouts goes to `stdout`;

```
nfqlb trace --selection=log,packet,frag
# Or;
nfqlb trace --mask=0xffffffff   # trace everything
```


## Trace-flows

Per-packet printouts should normally be avoided but may sometimes be
necessary. The flows in `nfqlb` are re-use to select packets to
trace;

```
  trace-flow-set
  trace-flow-delete
  trace-flow-list
  trace-flow-list-names
```

**NOTE**; that only packets in the *ingress* direction is seen by the
  `nfqlb`, so you will *not* see any egress packets.

The `trace-flow-set` is basically the same as `flow-set` but lacks
some options that doesn't apply for trace-flows;

```
# nfqlb trace-flow-set -h
trace-flow-set [options]
  Set a flow. An un-defined value means match-all.
  Use comma separated lists for multiple items (no spaces)
  --name= Name of the flow (required)
  --protocols= Protocols. tcp, udp, sctp 
  --dsts= Destination CIDRs 
  --srcs= Source CIDRs 
  --dports= Destination port ranges 
  --sports= Source port ranges 
  --match= Bit-match statements 
```

While you *can* specify a "default" flow that will match *every*
packet, the idea with `trace-flows` is to narrow the selection. You may
use `--srcs` and `--dports` and possibly `--sports`.

```
xcluster_FLOW=yes ./nfqlb.sh test -nrouters=1 start_mtu_squeeze > $log
# On vm-201
#nfqlb trace-flow-set --name=default  # will match anything. Don't do that!
nfqlb trace-flow-set --name=from-test-server --srcs=20.0.0.0/24,1000::1:20.0.0.0/120
nfqlb trace-flow-list
```

Per-packet printouts only for packets that matches the trace-flows
can now be initated with;

```
nfqlb trace --selection=flows
```

Example;
```
# On test server
ping -I 20.0.0.0 10.0.0.0
# Trace printout;
Match for trace-flow: from-test-server
ICMP_ECHO: id=58880, seq=0
proto=icmp, len=84, ::ffff:20.0.0.0 0 -> ::ffff:10.0.0.0 0
target=nfqlb, fwmark=102

Match for trace-flow: from-test-server
ICMP_ECHO: id=58880, seq=1
proto=icmp, len=84, ::ffff:20.0.0.0 0 -> ::ffff:10.0.0.0 0
target=nfqlb, fwmark=102
...
```

The "match" option may also be used to narrow the matching
packets. Here is an example that matches TCP packets with the *only*
the SYN flag set, or the RST flag (and any other flag) set;

```
nfqlb trace-flow-set --name=SYN-only --match='tcp[12:2]&0x01ff=0x0002'
nfqlb trace-flow-set --name=RST --match='tcp[12:2]&0x0004=0x0004'
```


For some ICMP packets the addresses are from the "inner" packet, most
interresting for the `ICMP_DEST_UNREACH/FRAG_NEEDED` (ipv4) and
`ICMP6_PACKET_TOO_BIG` (ipv6) packets. Example;

```
Match for trace-flow: from-test-server
ICMP6_PACKET_TOO_BIG: mtu=1480
proto=tcp, len=1280, 1000::1:1400:3 45118 -> 1000::1 80
target=vm-003, fwmark=103
```

The printed match is the packet that *caused* the
`ICMP6_PACKET_TOO_BIG`, not the ICMP packet itself. This is the `nfqlb` [destunreach](https://github.com/Nordix/nfqueue-loadbalancer/blob/master/destunreach.md)
function in action.

Delete all trace-flows;
```
for n in $(nfqlb trace-flow-list-names | jq -r .[]); do nfqlb trace-flow-delete --name=$n; done
```


## Compile without log/trace

There is always a fear that the even if nothing is logged or traced
the numerous checks in the code will affect performance. For tests
log/trace can be disabled;

```
make clean
CFLAGS=-DNO_LOG make -j8
```
