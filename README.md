# nfqueue-loadbalancer

A load-balancer based on the NF_QUEUE iptables target

**[WIP]** Moved here from [ovl/load-balancer](https://github.com/Nordix/xcluster/tree/master/ovl/load-balancer)

The `-j NFQUEUE` iptables target directs packets to a user-space
program. The program can analyze the packet, set `fwmark` and place a
"verdict".

<img src="nfqueue.svg" alt="NFQUEUQE packet path" width="75%" />

The `nfqlb lb` program receives packets and uses a configuration in
shared memory to compute a `fwmark`. The `nfqlb` program invoked with
other commands configures the shared memory. This decouples the
traffic handling from configuration. For instance when a real-target
is lost it must be removed from the configuration with;

```
nfqlb deactivate 3
```

Automatic detection and configuration is *not* a part of the
`nfqueue-loadbalancer`. You must do that yourself.

Hashing and fragment handling is done in the same way as for the
Google load-balancer,
[Maglev](https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/44824.pdf).

<img src="lb-tier.svg" alt="load-balancer tier" width="75%" />

The `nfqlb` is scalable, a property inherited from the Maglev
load-balancer. Since the configuration is the same for all nfqlb's it
does not matter which instance that receives a packet.

The forwarding of packets is done by normal Linux routing. The `nfqlb`
just sets an `fwmark`. That gives you possibility to use any Linux
routing to route packets to your targets. The simplest way is to use
routing "rules". Example;

```
ip rule add fwmark 1 table 1
ip route add default via 192.168.1.1 table 1
```
