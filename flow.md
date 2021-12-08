# Nordix/nfqueue-loadbalancer - Flows

A `flow` is defined by the 5-tuple; (proto,src,dst,sport,dport).

`nfqlb` can have multiple target load-balancers and there
is a mapping;

```
flow -> target load-balancer
```

All items in a flow are sets. For ports that means multiple ports and
port ranges. For addresses that means multiple CIDR's.

The target load-balancer is identified by it's shared mem name. The
configuration of a target load-balancer is made in the same way as
without flows.

The same fragmentation table, fragment injection `tun` device and
load-balancer tier is used by all target load-balancers.

Configuration example (nft/iptables config omitted);
```
# Start flow based load-balancer
nfqlb flowlb &
# Now the nfqlb is running without flows
# Create a target load-balancer
nfqlb init --shm=lb-1
nfqlb activate --shm=lb-1 101 103 103 104
# Add flows and tie them to the target load-balancer
nfqlb flow-set --name=flow-1 --prio=100 --targetShm=lb-1 --proto=udp,tcp \
  --dst=10.0.0.0/32,1000::/128 --dport=22,200-300,44,20000 \
  --src=2000::/64,192.168.2.0/24 --sport=20000-30000
nfqlb flow-set --name=flow-2 --prio=50 --targetShm=lb-1 --proto=sctp \
  --dst=10.0.0.0/32,1000::/128 --dport=4000 --udpencap=9899
nfqlb flow-delete --name=flow-2
```

There is no way to modify an existing flow, for instance removing a
port range, but a flow may be re-configured with an updated
configuration.

Unlike the lb-configuration (MaglevData) flows are stored in the lb
process and must be re-configured if the lb process is restarted.


## Performance

Flows are traversed one-by-one in priority order for each packet. With
many complex flows **the performance impact can be very large!**


## All-protocols flows

If no specific protocols are specified load-balancing is based on
addresses only (L3 level) and any (L4) protocol is accepted. `ping`
will work only with all-protocols flows.


## The inner-packet problem

The original idea was to use iptables/nft to direct flows to different
`nfqlb` instances by selecting different nfqueues. That does however
not work for incoming icmp replies with an "inner-packet", e.g
"fragmentation needed" used in PMTU discovery, since iptables/nft does
not support rules for inner-packets.

This was a driving reason for introducing flows in `nfqlb`.

## Limitations

* No parameter argument may be longer than 1000 chars
  This puts constraints on the max name, port-ranges etc
* Name may only contain alphanum and "-+_".
* Cidrs <= 32. These are traversed in linear for each packet
* If ports are specified, protocols must also be specified
* Target is a file-name and is restricted by Linux to 255 chars
* Port ranges may not include the "any" port (0) or be above USHRT_MAX
* If udpencap is specified the protocols must be "sctp" (only)
