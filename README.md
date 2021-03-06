# Nordix/nfqueue-loadbalancer

A load-balancer based on the
[NFQUEUE](https://home.regit.org/netfilter-en/using-nfqueue-and-libnetfilter_queue/comment-page-1/)
iptables target. The `-j NFQUEUE` target directs packets to a
user-space program. The program can analyze the packet and set
`fwmark` and `verdict`.

<img src="nfqueue.svg" alt="NFQUEUQE packet path" width="75%" />

The `nfqlb lb` program receives packets and uses a configuration in
shared memory to compute a `fwmark`. The `nfqlb` program invoked with
other commands configures the shared memory. This decouples the
traffic handling from configuration.

The `fwmark` represents a real-target and forwarding of packets is
done by Linux routing or NAT based on the `fwmark`. A basic
setup example;

```
# initiate the shared mem structure
nfqlb init
# Redirect packets to the VIP address to the nfqueue
iptables -t mangle -A PREROUTING -d 10.0.0.1/32 -j NFQUEUE --queue-num 2

# Add NAT/routing and activate fwmark (repeat for all real-targets)
if test "$DSR" = "yes"; then
  # Use routing for Direct Server Return (DSR)
  ip rule add fwmark 1 table 1
  ip route add default via 192.168.1.1 table 1
else
  # NAT based load-balancing
  iptables -t nat -A POSTROUTING -m mark --mark 1 -j DNAT --to-destination 192.168.1.1
fi
nfqlb activate 1

# Check the config and start load-balancing
nfqlb show
nfqlb lb
```

When a real-target is lost it must be removed from the configuration
with;

```
nfqlb deactivate 5
```

Automatic detection and re-configuration when a target (or
load-balancer) is lost/added is *not* a part of
`nfqueue-loadbalancer`. You must do that in your own way.

<img src="lb-tier.svg" alt="load-balancer tier" width="75%" />


Hashing and fragment handling is done in the same way as the Google
load-balancer; [Maglev](maglev.md). `nfqlb` is stateless and since
the configuration is the same for all instances it does not matter where
packets are received. This makes `nfqlb` scalable.


### More info

* [Maglev](maglev.md) - How functions from the Google load-balancer is used
* [Fragment handling](fragments.md) - How fragmented packets are handled
* [Improved TCP performance](syn-only.md) - For TCP only `SYN` packets may be load-balanced
* [Fragment tracking](fragtrack.md) - With another hash table
* [Testing](test/README.md) - Unit, function and performance testing
* [Destination Unreachable icmp](destunreach.md) - The PMTU discovery problem
* [SCTP](sctp.md) - SCTP load-balancing with multihoming
* [extend/alter](src/README.md) - Extend or alter `nfqlb`
* [Flows](flow.md) - Define LBs for flows, (proto,src,dst,sport,dport) tuples
* [Log/trace](log-trace.md) - Log and trace

## Try it

Try it on your own machine using Docker containers.

You must have some targets. We use docker containers, but anything
with an ip-address (not loopback) will do.

Bring up some container targets;
```
for n in 1 2 3; do
  name=target$n
  docker run -d --hostname=$name --name=$name --rm alpine:latest nc -nlk -p 8888 -e hostname
  docker inspect $name | jq .[].NetworkSettings.Networks.bridge.IPAddress
  #docker inspect $name | jq .[].NetworkSettings.Networks.bridge.GlobalIPv6Address
done
nc <addr> 8888   # Test connectivity
```

You *can* setup load-balancing in your main network name-space but it
is better to use another container when testing. Start the test
container and enter. You must use `--privileged` for network
configuration.

```
docker run --privileged -it --rm registry.nordix.org/cloud-native/nfqlb:latest /bin/sh
```

Use the `nfqlb.sh lb` script to setup load-balancing.  The setup is
NAT based like the example above, check the `cmd_lb()` function in
[nfqlb.sh](nfqlb.sh) for details.


In the test container;
```
PATH=$PATH:/opt/nfqlb/bin
nfqlb.sh lb --vip=10.0.0.0/32 <your container ip addresses here...>
# (example; nfqlb.sh lb --vip=10.0.0.0/32 172.17.0.3 172.17.0.4 172.17.0.5)

# Check load-balancing;
for n in $(seq 1 20); do echo | nc 10.0.0.0 8888; done

# Check some things
iptables -t nat -S    # OUTPUT chain for local origin, forwarding is not setup!
iptables -t mangle -S # The VIP is routed to user-space
nfqlb show            # Shows the Maglev hash lookup
nfqlb deactivate 101  # Deactivates a target. Check load-balancing again!
```

Stop the targets;
```
docker stop target1 target2 target3
```

This is a basic example. `nfqlb` can load-balance forwarded traffic
also (of course) and do Direct Server Return (DSR). This will however
require a different setup. But the setup in this example *can* be used
for IPv6. You must setup ipv6 support in `docker` and use ipv6
addresses for the VIP and targets in the `nfqlb.sh lb` command. But
that is left as an exercise for the reader.


You can also test with [xcluster](https://github.com/Nordix/xcluster)
using the [function tests](test/README.md).



## Build

```
make -C src help
make -C src -j8
./nfqlb.sh build_image    # Build the test docker image

```
Linked with `-lmnl -lnetfilter_queue` so you must install those.
Libpcap is used in unit-tests.
```
sudo apt install -y libmnl-dev libnetfilter-queue-dev libpcap-dev
```

Static binary;
```
# On Ubuntu 20.04
./nfqlb.sh libnfqueue_download
./nfqlb.sh libnfqueue_unpack
./nfqlb.sh libnfqueue_build
# On Ubuntu 22.04
./nfqlb.sh libmnl_download
./nfqlb.sh libmnl_unpack
./nfqlb.sh libmnl_build

make -C src clean
make -C src -j8 static
strip /tmp/$USER/nfqlb/nfqlb/nfqlb
file /tmp/$USER/nfqlb/nfqlb/nfqlb
./nfqlb.sh build_alpine_image   # Will build a static binary
```

The static libs are not present for `libnfqueue-queue-dev` in Ubuntu
20.04. However they *are* present in Ubuntu 22.04, but then the static
libs are not in `libmnl-dev` (sigh...). This bug is
[reported](https://bugs.launchpad.net/ubuntu/+source/libmnl/+bug/1971523).
