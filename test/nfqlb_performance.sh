#! /bin/sh
##
## nfqlb_performance.sh --
##
##   Performance test scriptlets for;
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

## Low-level Commands;

##   start_iperf_server [--multi-src]
##     In the current netns. Both tcp6 and udp6. --multi-src requires
##     a patched iperf.
cmd_start_iperf_server() {
	local iperf=iperf
	if test "$__multi_src" = "yes"; then
		iperf="$HOME/Downloads/iperf"
		test -x $iperf || die "Not executable [$iperf]"
		iperf="$iperf --sum-dstip"
	fi
	if netstat -putln 2> /dev/null | grep 5001 | grep -q tcp6; then
		log "Iperf tcp6 already started"
	else
		$iperf --server --daemon --ipv6_domain || die
	fi
	if netstat -putln 2> /dev/null | grep 5001 | grep -q udp6; then
		log "Iperf udp6 already started"
	else
		$iperf --server --daemon --ipv6_domain --udp || die
	fi
}

##   multi_src_route [--sudo] [--remove]
##     Add (or remove) router to the multi-address space.
cmd_multi_src_route() {
	test "$__sudo" = "yes" && __sudo=sudo
	if test "$__remove" = "yes"; then
		$__sudo ip ro del 10.200.200.0/24
	else
		$__sudo ip ro replace 10.200.200.0/24 via $(cmd_container_address nfqlb)
	fi
}

##   start_test_image
##     Re-start the test container.
cmd_start_test_image() {
	docker stop -t 1 nfqlb > /dev/null 2>&1
	docker run --privileged --name=nfqlb -d --rm registry.nordix.org/cloud-native/nfqlb:latest
}

cmd_docker_address() {
	docker inspect bridge | jq -r .[].IPAM.Config[].Gateway | grep -v :
}

cmd_container_address() {
	docker inspect "$1" | jq -r .[].NetworkSettings.IPAddress
}

##   start_lb [--adr=] [--vip=]
##     Start nfqlb in the test container
cmd_start_lb() {
	test -n "$__adr" || __adr=$(cmd_docker_address)
	test -n "$__vip" || __vip=10.0.0.0/32
	docker exec --detach nfqlb /opt/nfqlb/bin/nfqlb.sh lb --queue=$__queue --vip=$__vip $__adr
}

##   iperf <params...>
##     Run iperf in the test container
cmd_iperf() {
	docker exec -it nfqlb iperf $@
}

##   qstats
##     Format printout of /proc/net/netfilter/nfnetlink_queue
cmd_qstats() {
	local sfile=/proc/net/netfilter/nfnetlink_queue
	echo "  Q       port inq cp   rng  Qdrop  Udrop      Seq"
	docker exec nfqlb cat $sfile | format_stats
}
format_stats() {
	local line s
	while read line; do
		s=$(echo $line | tr -s ' ' :)
		# Q port(pid) inQ cpmode range Qdroped usrDropped lastSeq 1
		local Q=$(echo $s | cut -d: -f1)
		local port=$(echo $s | cut -d: -f2)
		local inQ=$(echo $s | cut -d: -f3)
		local cp=$(echo $s | cut -d: -f4)
		local rng=$(echo $s | cut -d: -f5)
		local Qdrop=$(echo $s | cut -d: -f6)
		local Udrop=$(echo $s | cut -d: -f7)
		local seq=$(echo $s | cut -d: -f8)
		printf "%3u %10u %3u  %u %5u %6u %6u %8u\n"\
			$Q $port $inQ $cp $rng $Qdrop $Udrop $seq
	done
}

# CPU usage;
# https://stackoverflow.com/questions/23367857/accurate-calculation-of-cpu-usage-given-in-percentage-in-linux

cmd_cpu_sample() {
	local s=$(head -1 /proc/stat | tr -s ' ' :)
	local user=$(echo $s | cut -d: -f2)
	local nice=$(echo $s | cut -d: -f3)
	local system=$(echo $s | cut -d: -f4)
	local idle=$(echo $s | cut -d: -f5)
	local iowait=$(echo $s | cut -d: -f6)
	local irq=$(echo $s | cut -d: -f7)
	local softirq=$(echo $s | cut -d: -f8)
	local steal=$(echo $s | cut -d: -f9)
	local guest=$(echo $s | cut -d: -f10)
	local guest_nice=$(echo $s | cut -d: -f11)
	echo "$((user + nice + system + irq + softirq + steal)):$((idle + iowait))"
}
cmd_cpu_usage_since() {
	local s=$(cmd_cpu_sample)  # (asap!)
	test -n "$1" || die "No previous sample"
	local pbusy=$(echo $1 | cut -d: -f1)
	local pidle=$(echo $1 | cut -d: -f2)
	local ptotal=$((pbusy + pidle))
	local busy=$(echo $s | cut -d: -f1)
	local idle=$(echo $s | cut -d: -f2)
	local total=$((busy + idle))

	local totald=$((total - ptotal))
	local idled=$((idle - pidle))
	local pecent10=$(( 1000 * (totald - idled) / totald))
	echo "$((pecent10/10)).$((pecent10%10))%"
}

cmd_add_multi_address() {
	docker exec nfqlb /opt/nfqlb/bin/nfqlb.sh multi_address
}

##
## Test Commands;

##   test_netns [--iface= --ipvlan-addr=] [--delete]
##     Create netns for frag test. If --iface= is specified an ipvlan is used.
cmd_test_netns() {
	test -n "$__sudo" || __sudo=sudo
	if test "$__delete" = "yes"; then
		test_netns_delete
		return
	fi
	local netns=${USER}_nfqlb
	local ip="$__sudo ip"
	$ip netns pid $netns > /dev/null 2>&1 && die "Netns exists [$netns]"
	$ip netns add $netns || die "Failed to add netns"
	$ip netns exec $netns ip link set up dev lo
	$ip link add nfqlb0 type veth peer name host0 || die veth
	$ip link set dev host0 netns $netns || die host0
	$ip link set up dev nfqlb0 || die "ip up"
	$ip addr add 10.20.0.0/31 dev nfqlb0 || die "ip addr"
	$ip -6 addr add 1000::1:10.20.0.0/127 dev nfqlb0 || die "ip -6 addr"
	$ip netns exec $netns ip link set up dev host0
	$ip netns exec $netns ip addr add 10.20.0.1/31 dev host0
	$ip netns exec $netns ip -6 addr add 1000::1:10.20.0.1/127 dev host0

	if test -n "$__iface"; then
		die NYI
	fi

	# Setup a server netns
	local srvnetns=${USER}_nfqlb_server
	$ip netns add $srvnetns || die "Failed to add [$srvnetns]"
	$ip netns exec $srvnetns ip link set up dev lo

	$ip link add ext0 type veth peer name ns0 || die veth
	$ip link set dev ext0 netns $netns || die ext0
	$ip link set dev ns0 netns $srvnetns || die ns0

	$ip netns exec $netns ip link set up dev ext0
	$ip netns exec $srvnetns ip link set up dev ns0
	$ip netns exec $netns ip addr add 10.10.0.1/31 dev ext0
	$ip netns exec $netns ip -6 addr add 1000::1:10.10.0.1/127 dev ext0
	$ip netns exec $srvnetns ip addr add 10.10.0.0/31 dev ns0
	$ip netns exec $srvnetns ip -6 addr add 1000::1:10.10.0.0/127 dev ns0

}
test_netns_delete() {
	local ip="$__sudo ip"
	$ip netns del ${USER}_nfqlb || die "netns del"
	$ip link del nfqlb0

	if test -n "$__iface"; then
		die NYI
	fi

	$ip netns del ${USER}_nfqlb_server
}

##   test_hw_server [--multi-src --clientip= --vip=] [iperf options...]
##   test_hw_client --serverip= --vip= [--multi-src] [iperf options...]
##     DSR is used for --multi-src.
cmd_test_hw_server() {
	if test "$__multi_src" != "yes"; then
		exec iperf -s -V $@
		return					# (never reached)
	fi

	# multi-src
	test -n "$__sudo" || __sudo=sudo
	local iperf=$HOME/Downloads/iperf
	test -x $iperf || die "Not executable [$iperf]"
	test -n "$__clientip" || die "Not set --clientip="
	ping -c1 -W1 -nq $__clientip > /dev/null || die "Can't ping [$__clientip]"

	local ip="$__sudo ip"
	local srccidr=10.200.200.0/24
	if echo "$__clientip" | grep -q :; then
		ip="$__sudo ip -6"
		srccidr=1000::1:10.200.200.0/120
	fi

	$ip route replace $srccidr via $__clientip
	$ip addr add $__vip dev lo
	echo "Cleanup;"
	echo "  $ip route del $srccidr via $__clientip"
	echo "  $ip addr del $__vip dev lo"
	echo "Starting; iperf --sum-dstip --server --ipv6_domain $@"
	$iperf --sum-dstip --server --ipv6_domain $@
}
basic_hw_client() {
	#sudo ip -6 ro add default via fd01::2
	test -n "$__nfqlb" || __nfqlb=/tmp/$USER/nfqlb/nfqlb/nfqlb
	test -x "$__nfqlb"|| die "Not executable [$__nfqlb]"
	local nfqlbsh=${__nfqlb}.sh
	test -x $nfqlbsh || nfqlbsh=$dir/nfqlb.sh
	test -x $nfqlbsh || nfqlbsh=$(readlink -f $dir/..)/nfqlb.sh
	test -x $nfqlbsh || die "Not executable [$nfqlbsh]"
	local xopt ip="$__sudo ip"
	if echo "$__serverip" | grep -q :; then
		xopt=-V
		ip="$__sudo ip -6"
	fi
	
	local s i=0
	if ! $ip ro get $__vip > /dev/null 2>&1; then
		i=$((i+1)); echo "$i. Setup route to $__vip"
		$ip ro add $__vip via $__serverip
	fi
	i=$((i+1)); echo "$i. Start LB"
	$__sudo $nfqlbsh lb --path=$(dirname $__nfqlb) --vip=$__vip $__serverip
	i=$((i+1)); echo "$i. Iperf direct (-c $__serverip $xopt $@)"
	s=$(cmd_cpu_sample)
	iperf -c $__serverip $xopt $@   # direct
	i=$((i+1)); echo "$i. CPU usage $(cmd_cpu_usage_since $s)"
	i=$((i+1)); echo "$i. Iperf VIP (-c $__vip $xopt $@)"
	local vip=$(echo $__vip | cut -d/ -f1)
	s=$(cmd_cpu_sample)
	iperf -c $vip $xopt $@        # via nfqlb
	i=$((i+1)); echo "$i. CPU usage $(cmd_cpu_usage_since $s)"
	i=$((i+1)); echo "$i. Stop LB"
	$__sudo $nfqlbsh stop_lb --path=$(dirname $__nfqlb) --vip=$__vip $__serverip
	$ip ro del $__vip via $__serverip > /dev/null 2>&1
}
cmd_test_hw_client() {
	test -n "$__serverip" || die "No server ip"
	ping -c1 -W1 -nq $__serverip > /dev/null || die "Can't ping [$__serverip]"
	test -n "$__vip" || die "No VIP"
	test -n "$__sudo" || __sudo=sudo
	local ip="$__sudo ip"
	echo "$__serverip" | grep -q : && ip="$__sudo ip -6"

	if test "$__multi_src" != "yes"; then
		basic_hw_client $@
		return
	fi

	$ip ro get $__vip > /dev/null || die "No route to [$__vip]"

	# multi-src
	local srccidr=10.200.200.0/24
	local src=10.200.200.1
	if echo "$__serverip" | grep -q :; then
		srccidr=1000::1:10.200.200.0/120
		src=1000::1:10.200.200.1
	fi

	local xopt
	iperf=$HOME/Downloads/iperf
	test -x $iperf || die "Not executable [$iperf]"
	xopt="-B $src --incr-srcip"
	i=$((i+1)); echo "$i. Add addresses to dev lo"
	$__sudo $ip addr add $srccidr dev lo

	local s
	local i=0


	
	i=$((i+1)); echo "$i. Remove addresses from dev lo"
	$__sudo $ip addr del $srccidr dev lo
}

##   test [--rebuild] [--no-stop] [--multi-src] [iperf options...]
##     Use the test container
cmd_test() {
	local xopt
	local s
	local i=0
	i=$((i+1)); echo "$i. Start iperf servers"
	cmd_start_iperf_server > /dev/null 2>&1
	if test "$__rebuild" = "yes"; then
		i=$((i+1)); echo "$i. Rebuild the test container"
		$nfqlbsh build_alpine_image > /dev/null || die
	fi
	i=$((i+1)); echo "$i. Start the test container"
	cmd_start_test_image > /dev/null
	if test "$__multi_src" = "yes"; then
		i=$((i+1)); echo "$i. Add multiple addresses in the container"
		cmd_add_multi_address
		xopt="-B 10.200.200.1 --incr-srcip"
		i=$((i+1)); echo "$i. Add routes to multi-addresses"
		cmd_multi_src_route
	fi
	i=$((i+1)); echo "$i. Start LB"
	cmd_start_lb
	i=$((i+1)); echo "$i. Iperf direct (-c $(cmd_docker_address) $xopt $@)"
	s=$(cmd_cpu_sample)
	cmd_iperf -c $(cmd_docker_address) $xopt $@
	i=$((i+1)); echo "$i. CPU usage $(cmd_cpu_usage_since $s)"
	i=$((i+1)); echo "$i. Nfnetlink_queue stats"
	cmd_qstats
	i=$((i+1)); echo "$i. Re-start iperf servers"
	cmd_start_iperf_server > /dev/null 2>&1
	local vip=$(echo $__vip | cut -d/ -f1)
	i=$((i+1)); echo "$i. Iperf VIP (-c $vip $xopt $@)"
	cmd_iperf -c $vip $xopt $@
	i=$((i+1)); echo "$i. CPU usage $(cmd_cpu_usage_since $s)"
	i=$((i+1)); echo "$i. Nfnetlink_queue stats"
	cmd_qstats
	if test "$__fragstats" = "yes"; then
		i=$((i+1)); echo "$i. Get frag stats"
		docker exec nfqlb /opt/nfqlb/bin/nfqlb stats
	fi
	if test "$__multi_src" = "yes"; then
		i=$((i+1)); echo "$i. Remove routes to multi-addresses"
		__remove=yes
		cmd_multi_src_route
	fi		
	if test "$__no_stop" != "yes"; then
		i=$((i+1)); echo "$i. Stop the container"
		docker stop -t 1 nfqlb > /dev/null 2>&1
	fi
}
#   test_report [iperf options...]
cmd_test_report() {
	echo "# Nfqlb test report - iperf $@"
	echo '```'
	cmd_test $@
	echo '```'
}


##

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
