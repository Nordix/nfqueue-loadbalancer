# Nordix/nfqueue-loadbalancer - testing

Unit tests are just local programs, function tests uses
[xcluster](https://github.com/Nordix/xcluster) and performance tests uses
real HW.

## Unit tests

Unit tests can be written in any way, no "framework" is imposed. Unit
test programs must exit with zero on success and non-zero on failure.

```
cd src
make clean; CFLAGS="-Werror" make -j8 test
make clean; CFLAGS="-DVERBOSE -DSANITY_CHECK -Werror" make -j8 test
```

Test programs are in `src/lib/test`. Any file with the pattern
`*-test.c` will be compiled and executed on `make test`. Currently
simple `assert`s are used.

### Dependency injection

The [dependency injection](https://en.wikipedia.org/wiki/Dependency_injection)
pattern is used to inject the current time, example;

```c
void* ctLookup(
    struct ct* ct, struct timespec* now, struct ctKey const* key);
```

This makes it possible to test anything down to nano-second level and
to do long virtual time simulations in really short real-time.


### Simulations

This is a special case of unit tests used to find a
configuration for the [fragtrack table](../fragtrack.md#configuration).



## Function test

Install [xcluster](https://github.com/Nordix/xcluster);
```
# Download the latest release, at least `v5.4.7`
tar xf ~/Downloads/xcluster-v5.4.7.tar.xz
cd xcluster
. ./Envsettings
nfqlb_dir=/your/path/to/Nordix/nfqueue-loadbalancer
export XCLUSTER_OVLPATH=$(readlink -f .)/ovl:$nfqlb_dir/test/ovl
```

The function test will use the
[mconnect](https://github.com/Nordix/mconnect) and
[ctraffic](https://github.com/Nordix/ctraffic) tests programs and the
*awsome* [jq](https://stedolan.github.io/jq/) utility.

```
curl -L https://github.com/Nordix/mconnect/releases/download/v2.2.0/mconnect.xz > $HOME/Downloads/mconnect.xz
curl -L https://github.com/Nordix/ctraffic/releases/download/v1.4.0/ctraffic.gz > $HOME/Downloads/ctraffic.gz
sudo apt install jq
```


Then proceed with the function tests in [ovl/nfqlb](ovl/nfqlb);
```
cdo nfqlb
log=/tmp/$USER/xcluster-test.log
./nfqlb.sh test > $log
```


## Performance test

We want to measure the impact on throughput, latency and packet loss
caused by the nfqueue. So we compare direct traffic and traffic
through the `nfqlb` to one single target.

The easiest way, and probably a quite good one, is to use the Docker
image we used in the example. We set our `docker0` device in main
netns as the one target and run `iperf` directly and to the VIP
address;

```
# Start an iperf server in main netns
iperf -s -V
# In another shell;
./nfqlb.sh build_image    # Build the test docker image
docker run --privileged -it --rm nordixorg/nfqlb:latest /bin/bash
# In the container;
PATH=$PATH:/opt/nfqlb/bin
nfqlb.sh lb --vip=10.0.0.0/32 172.17.0.1
iperf -c 172.17.0.1
iperf -c 10.0.0.0
```

The measurements from the very first test without any optimizations
was actually not that bad;


```
$ iperf -c 172.17.0.1
------------------------------------------------------------
Client connecting to 172.17.0.1, TCP port 5001
TCP window size: 1.22 MByte (default)
------------------------------------------------------------
[  3] local 172.17.0.3 port 45058 connected with 172.17.0.1 port 5001
[ ID] Interval       Transfer     Bandwidth
[  3]  0.0-10.0 sec  58.2 GBytes  50.0 Gbits/sec
$ iperf -c 10.0.0.0
------------------------------------------------------------
Client connecting to 10.0.0.0, TCP port 5001
TCP window size: 1.97 MByte (default)
------------------------------------------------------------
[  3] local 172.17.0.3 port 57446 connected with 10.0.0.0 port 5001
[ ID] Interval       Transfer     Bandwidth
[  3]  0.0-10.1 sec  40.9 GBytes  34.7 Gbits/sec
$ cat /proc/net/netfilter/nfnetlink_queue
#   Q    pid   inQ cp cprng Qdrop usrdrop    idseq  ?
    2     43     0  2 65531     0   12846   975255  1
```

Direct traffic `50.0 Gbits/sec`, through nfqlb `34.7 Gbits/sec`. The
biggest problem seem to be "user dropped".


Iperf3 is not used since it's [not intended for use with load-balancers](https://github.com/esnet/iperf/issues/823).


### Links

* https://home.regit.org/netfilter-en/using-nfqueue-and-libnetfilter_queue/comment-page-1/

