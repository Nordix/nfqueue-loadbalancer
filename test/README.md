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
container we used in the example. We set our `docker0` device in main
netns as the one target and run `iperf` directly and to the VIP
address. A problem is that the example container uses DNAT
so [fragment tests are not
possible](https://github.com/Nordix/nfqueue-loadbalancer/blob/master/fragments.md#the-unwanted-re-assembly-problem).

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

Iperf3 is not used since it's [not intended for use with load-balancers](https://github.com/esnet/iperf/issues/823).

Automatic test using the `nfqlb_performance.sh` script;
```
$ ./nfqlb_performance.sh test
1. Start iperf servers
2. Start the test container
3. Start LB
4. Iperf direct
------------------------------------------------------------
Client connecting to 172.17.0.1, TCP port 5001
TCP window size: 85.0 KByte (default)
------------------------------------------------------------
[  1] local 172.17.0.3 port 54718 connected with 172.17.0.1 port 5001
[ ID] Interval       Transfer     Bandwidth
[  1] 0.00-10.00 sec  54.6 GBytes  46.9 Gbits/sec
5. CPU usage 23.5%
6. Nfnetlink_queue stats
  Q       port inq cp   rng  Qdrop  Udrop      Seq
  2         72   0  2  1280      0      0        0
7. Re-start iperf servers
8. Iperf VIP
------------------------------------------------------------
Client connecting to 10.0.0.0, TCP port 5001
TCP window size: 85.0 KByte (default)
------------------------------------------------------------
[  1] local 172.17.0.3 port 44948 connected with 10.0.0.0 port 5001
[ ID] Interval       Transfer     Bandwidth
[  1] 0.00-10.00 sec  60.2 GBytes  51.7 Gbits/sec
9. CPU usage 28.9%
10. Nfnetlink_queue stats
  Q       port inq cp   rng  Qdrop  Udrop      Seq
  2         72   0  2  1280      0      0  1449321
10. Stop the container
```

There is no bandwidth degradation caused by `nfqlb` but there *is* a
CPU usage increase. At ~50 Gbits/sec it is ~6% which is acceptable. It
would also be lower on a more powerful machine than my Dell ultrabook.


### Parallel and multi-queue

You can start `iperf` with parallel connections ([report](report-P8.md)):
```
./nfqlb_performance.sh test -P8
```

Now direct traffic uses all cores (I have 8) and the throughput
becomes ~80 Gbits/sec. But via `nfqlb` the throughput stays at ~40
Gbits/sec. This because only a single thread handles packets in
`nfqlb`. To use multi-queue (and multi-thread) is supported but does
not help since `iperf` uses the same addresses for all connections and
they belongs to the same "flow" and goes to the same queue and we get
`user drops` ([report](report-P8-mqueue.md)).

```
./nfqlb_performance.sh test --queue=0:3 -P8
```

If you have a cpu monitor running you can see that one core get 100%
load.

### UDP

It is not simple to test UPD bandwidth with `iperf`. Basically you
have to set the bandwidth using the `-b` flag and check what happens
([report](report-udp2G.md));

```
./nfqlb_performance.sh test -b2G -u
```

If we try `-b4G` we can notice that direct access stays at ~3G while
traffic through `nfqlb` stays around ~2G ([report](report-udp4G.md)).

The difference compared to TCP feels too large. We must probably find
another tool for testing UDP bandwidth.


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

