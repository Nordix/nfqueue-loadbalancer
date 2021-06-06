#! /bin/sh
##
## nfqlb_performance.sh --
##
##   Perfoemance test scriptlets for;
##   https://github.com/Nordix/nfqueue-loadbalancer/
##

prg=$(basename $0)
dir=$(dirname $0); dir=$(readlink -f $dir)
tmp=/tmp/${prg}_$$
me=$dir/$prg
nfqlbsh=$(readlink -f $dir/../nfqlb.sh)

die() {
    echo "ERROR: $*" >&2
    rm -rf $tmp
    exit 1
}
help() {
    grep '^##' $0 | cut -c3-
    rm -rf $tmp
    exit 0
}
test -n "$1" || help
echo "$1" | grep -qi "^help\|-h" && help

log() {
	echo "$prg: $*" >&2
}
dbg() {
	test -n "$__verbose" && echo "$prg: $*" >&2
}

##   start_iperf_server
##     In the current netns. Both tcp6 and udp6
cmd_start_iperf_server() {
	if netstat -putln 2> /dev/null | grep 5001 | grep -q tcp6; then
		log "Iperf tcp6 already started"
	else
		iperf --server -P 8 --daemon --ipv6_domain || die
	fi
	if netstat -putln 2> /dev/null | grep 5001 | grep -q udp6; then
		log "Iperf udp6 already started"
	else
		iperf --server -P 8 --daemon --ipv6_domain --udp || die
	fi
}

##   start_test_image
##     Re-build test image and re-start the test container
cmd_start_test_image() {
	docker stop -t 1 nfqlb > /dev/null 2>&1
	$nfqlbsh build_alpine_image || die
	docker run --privileged --name=nfqlb -d --rm registry.nordix.org/cloud-native/nfqlb:latest
}

cmd_docker_address() {
	docker inspect bridge | jq -r .[].IPAM.Config[].Gateway | grep -v :
}

##   start_lb [--adr=] [--vip=]
##     Start nfqlb in the test container
cmd_start_lb() {
	test -n "$__adr" || __adr=$(cmd_docker_address)
	test -n "$__vip" || __vip=10.0.0.0/32
	docker exec --detach nfqlb /opt/nfqlb/bin/nfqlb.sh lb --vip=$__vip $__adr
}

##   iperf <params...>
cmd_iperf() {
	docker exec -it nfqlb iperf $@
}

##   qstats
cmd_qstats() {
	local sfile=/proc/net/netfilter/nfnetlink_queue
	echo "  Q  port inq cp   rng  Qdrop  Udrop      Seq"
	docker exec nfqlb cat $sfile | format_stats
}
format_stats() {
	local s=$(cat | tr -s ' ' :)
	# Q port(pid) inQ cpmode range Qdroped usrDropped lastSeq 1
	local Q=$(echo $s | cut -d: -f2)
	local port=$(echo $s | cut -d: -f3)
	local inQ=$(echo $s | cut -d: -f4)
	local cp=$(echo $s | cut -d: -f5)
	local rng=$(echo $s | cut -d: -f6)
	local Qdrop=$(echo $s | cut -d: -f7)
	local Udrop=$(echo $s | cut -d: -f8)
	local seq=$(echo $s | cut -d: -f9)
	printf "%3u %5u %3u  %u %4u %6u %6u %8u\n"\
		$Q $port $inQ $cp $rng $Qdrop $Udrop $seq
}

cmd_tcp() {
	echo "1. Start iperf servers"
	cmd_start_iperf_server > /dev/null 2>&1
	echo "2. Rebuild and restart test container"
	cmd_start_test_image > /dev/null
	echo "3. Start LB"
	cmd_start_lb
	echo "4. Iperf direct"
	cmd_iperf -c $(cmd_docker_address)
	echo "5. Nfnetlink_queue stats"
	cmd_qstats
	echo "6. Iperf VIP"
	local vip=$(echo $__vip | cut -d/ -f1)
	cmd_iperf -c $vip
	echo "7. Nfnetlink_queue stats"
	cmd_qstats
}

# Get the command
cmd=$1
shift
grep -q "^cmd_$cmd()" $0 $hook || die "Invalid command [$cmd]"

while echo "$1" | grep -q '^--'; do
    if echo $1 | grep -q =; then
	o=$(echo "$1" | cut -d= -f1 | sed -e 's,-,_,g')
	v=$(echo "$1" | cut -d= -f2-)
	eval "$o=\"$v\""
    else
	o=$(echo "$1" | sed -e 's,-,_,g')
	eval "$o=yes"
    fi
    shift
done
unset o v
long_opts=`set | grep '^__' | cut -d= -f1`

# Execute command
trap "die Interrupted" INT TERM
cmd_$cmd "$@"
status=$?
rm -rf $tmp
exit $status
