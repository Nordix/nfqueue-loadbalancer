# github.com/Nordix/nfqueue-loadbalancer - ovl/nfqlb

Function tests for the
[nfqueue-loadbalancer](https://github.com/Nordix/nfqueue-loadbalancer).

## Usage

```
#export __nrouters=1
test -n "$log" || log=/tmp/$USER/xcluster-test.log
# Start without any tests;
./nfqlb.sh test start > $log
# Fast TCP test ipv4 and ipv6
./nfqlb.sh test > $log
# Fragmented UDP traffic. Print stats
./nfqlb.sh test --verbose udp > $log
```
