# Nordix/nfqueue-loadbalancer - testing

Unit tests are just local programs, function tests uses
[xcluster](https://github.com/Nordix/xcluster) and performance tests uses
a Docker container.

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

Manual test;
```
# Start an iperf server in main netns
iperf -s -V
# In another shell;
docker run --privileged -it --rm registry.nordix.org/cloud-native/nfqlb:latest /bin/sh
# (check the address of your Docker network, usually on dev "docker0")
# In the container;
PATH=$PATH:/opt/nfqlb/bin
docker0adr=172.17.0.1
nfqlb.sh lb --vip=10.0.0.0/32 $docker0adr
iperf -c $docker0adr
iperf -c 10.0.0.0
```

Automatic test using the `nfqlb_performance.sh` script;
```
$ ./nfqlb_performance.sh tcp
1. Start iperf servers
2. Rebuild and restart test container
3. Start LB
4. Iperf direct
------------------------------------------------------------
Client connecting to 172.17.0.1, TCP port 5001
TCP window size: 85.0 KByte (default)
------------------------------------------------------------
[  1] local 172.17.0.3 port 33920 connected with 172.17.0.1 port 5001
[ ID] Interval       Transfer     Bandwidth
[  1] 0.00-10.00 sec  52.5 GBytes  45.1 Gbits/sec
5. Nfnetlink_queue stats
  Q  port inq cp   rng  Qdrop  Udrop      Seq
  2    59   0  2  1280      0      0        0
6. Iperf VIP
------------------------------------------------------------
Client connecting to 10.0.0.0, TCP port 5001
TCP window size: 85.0 KByte (default)
------------------------------------------------------------
[  1] local 172.17.0.3 port 42630 connected with 10.0.0.0 port 5001
[ ID] Interval       Transfer     Bandwidth
[  1] 0.00-10.00 sec  54.0 GBytes  46.4 Gbits/sec
7. Nfnetlink_queue stats
  Q  port inq cp   rng  Qdrop  Udrop      Seq
  2    59   0  2  1280      0      0  1224340
8. Stop the container
```

Iperf3 is not used since it's [not intended for use with load-balancers](https://github.com/esnet/iperf/issues/823).


### On HW

Warning: To run `nfqlb.sh lb` in main netns may interfere with your
network setup.

```
# On the remote machine (fd01::2);
iperf -s -V
# On the local machine
make -C src -j8
#sudo ip -6 ro add default via fd01::2
sudo ./nfqlb.sh lb --path=/tmp/$USER/nfqlb/nfqlb --vip=2000::1/128 fd01::2
iperf -V -c fd01::2      # direct
iperf -V -c 2000::1      # via nfqlb
sudo ./nfqlb.sh stop_lb --path=/tmp/$USER/nfqlb/nfqlb --vip=2000::1/128 fd01::2
```
Note: You *must* have a default route even though it's not used.

Test on a 1G interface shows ~800 Mbits/sec both with and without `nfqlb`.

