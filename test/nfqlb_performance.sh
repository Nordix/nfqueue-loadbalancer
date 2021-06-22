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
PREFIX=fd01:

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

##   start_lb [--adr=] [--vip=] [--queue=] [--lbopts=]
##     Start nfqlb in the test container
cmd_start_lb() {
	test -n "$__adr" || __adr=$(cmd_docker_address)
	test -n "$__vip" || __vip=10.0.0.0/32
	docker exec --detach nfqlb /opt/nfqlb/bin/nfqlb.sh lb --queue=$__queue --lbopts=$__lbopts --vip=$__vip $__adr
}

##   iperf <params...>
##     Run iperf in the test container
cmd_iperf() {
	docker exec -it nfqlb iperf $@
}

##   qstats [container]
##     Format printout of /proc/net/netfilter/nfnetlink_queue
cmd_qstats() {
	local sfile=/proc/net/netfilter/nfnetlink_queue
	echo "  Q       port inq cp   rng  Qdrop  Udrop      Seq"
	if test -n "$1"; then
		docker exec $1 cat $sfile | format_stats
	else
		test -n "$__sudo" || __sudo=sudo
		$__sudo cat $sfile | format_stats
	fi
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

##   test_netns [--iface=] [--delete]
##     Create netns for frag test. If --iface= is specified a macvlan is used,
##     otherwise a local nfqlb_server netns is created.
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
	$ip -6 addr add $PREFIX:10.20.0.0/127 dev nfqlb0 || die "ip -6 addr"
	$ip netns exec $netns ip link set up dev host0
	$ip netns exec $netns ip addr add 10.20.0.1/31 dev host0
	$ip netns exec $netns ip -6 addr add $PREFIX:10.20.0.1/127 dev host0

	$ip netns exec $netns sysctl -w net.ipv6.conf.all.forwarding=1 > /dev/null
	$ip netns exec $netns sysctl -w net.ipv4.conf.all.forwarding=1 > /dev/null
	$ip netns exec $netns sysctl -w net.ipv4.ip_forward=1 > /dev/null

	if test -n "$__iface"; then
		$ip link add link $__iface name ext0 type macvlan || die macvlan
		$ip link set ext0 netns $netns
		$ip netns exec $netns ip link set up ext0
		$ip netns exec $netns ip addr add 10.10.0.1/31 dev ext0
		$ip netns exec $netns ip -6 addr add $PREFIX:10.10.0.1/127 dev ext0
		return 0
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
	$ip netns exec $netns ip -6 addr add $PREFIX:10.10.0.1/127 dev ext0
	$ip netns exec $srvnetns ip addr add 10.10.0.0/31 dev ns0
	$ip netns exec $srvnetns ip -6 addr add $PREFIX:10.10.0.0/127 dev ns0
	$ip netns exec $srvnetns ip ro add default via 10.10.0.1
	$ip netns exec $srvnetns ip -6 ro add default via $PREFIX:10.10.0.1
}
test_netns_delete() {
	local ip="$__sudo ip"
	$ip netns del ${USER}_nfqlb || die "netns del"
	$ip link del nfqlb0

	test -n "$__iface" && return 0

	$ip netns del ${USER}_nfqlb_server
}

##   start_server --gw= [--vip=] [iperf options...]
##     Setup multi-src routing and start iperf servers.  If --vip= is
##     specified DSR is assumed.
cmd_start_server() {

	test -n "$__sudo" || __sudo=sudo
	test -n "$__gw" || die "Not set --gw="
	local iperf=$HOME/Downloads/iperf
	test -x $iperf || die "Not executable [$iperf]"
	ping -c1 -W1 -nq $__gw > /dev/null || die "Can't ping [$__gw]"

	local ip="$__sudo ip"
	local srccidr=10.200.200.0/24
	if echo "$__gw" | grep -q :; then
		ip="$__sudo ip -6"
		srccidr=$PREFIX:10.200.200.0/120
	fi
	$ip route replace $srccidr via $__gw

	test -n "$__vip" && $ip addr replace $__vip dev lo

	echo "Cleanup;"
	echo "  $ip route del $srccidr via $__gw"
	test -n "$__vip" && echo "  $ip addr del $__vip dev lo"
	echo "Starting; iperf --sum-dstip --server --ipv6_domain --daemon $@"
	__multi_src=yes
	cmd_start_iperf_server $@
}
##   dsr_test --vip= [--lbopts=] [iperf options...]
##     Setup addresses and routes for DSR and test.
##     Prerequisite; the netns must be setup and the dsr server running.
cmd_dsr_test() {
	test -n "$__vip" || die "No VIP"
	test -n "$__sudo" || __sudo=sudo
	find_progs
	test -n "$i" || i=0

	# Environment check
	i=$((i+1)); echo "$i. Environment check"
	local iperf=$HOME/Downloads/iperf
	test -x $iperf || die "Not executable [$iperf]"
	local ip="$__sudo ip"
	local gw=10.20.0.1
	local myip=10.20.0.0
	local server=10.10.0.0
	local srccidr=10.200.200.0/24
	local base=10.200.200.1
	local xopt="-B $base --incr-srcip"
	if echo $__vip | grep -q :; then
		ip="$__sudo ip -6"
		gw=$PREFIX:10.20.0.1
		myip=$PREFIX:10.20.0.0
		server=$PREFIX:10.10.0.0
		srccidr=$PREFIX:10.200.200.0/120
		base=$PREFIX:10.200.200.1
		xopt="-V -B $base --incr-srcip"
	fi
	ping -c1 -W1 -nq $gw > /dev/null || die "Can't ping the netns [$gw]"
	ip netns exec ${USER}_nfqlb ping -c1 -W1 -nq $server > /dev/null \
		|| die "Failed to ping the server from the netns [$server]"

	i=$((i+1)); echo "$i. Add route to VIP via the netns"
	$ip ro replace $__vip via $gw || die "Set route to [$__vip]"

	i=$((i+1)); echo "$i. Add addresses to dev lo"
	$nfqlbsh multi_address --sudo=$__sudo

	i=$((i+1)); echo "$i. Add routes inside the netns"
	ip netns exec ${USER}_nfqlb $ip ro replace $__vip via $server
	ip netns exec ${USER}_nfqlb $ip ro replace $srccidr via $myip

	i=$((i+1)); echo "$i. Ping the VIP from within the netns"
	local vip=$(echo $__vip | cut -d/ -f1)
	ip netns exec ${USER}_nfqlb ping -c1 -W1 -nq $vip > /dev/null \
		|| die "Ping vip failed from within the netns [$vip]"

	i=$((i+1)); echo "$i. Ping the VIP from main netns"
	ping -c1 -W1 -nq -I $base $vip > /dev/null \
		|| die "Ping vip failed from main netns [-I $base $vip]"

	if test "$__test_setup" = "yes"; then
		i=$((i+1)); echo "$i. Start nfqlb in the netns"
		ip netns exec ${USER}_nfqlb $me lb --vip=$__vip
		i=$((i+1)); echo "$i. Test-setup ready"
		return 0
	fi

	i=$((i+1)); echo "$i. Direct access (-c $vip $xopt $@)"
	local s=$(cmd_cpu_sample)
	$iperf -c $vip $xopt $@ || die "iperf direct"
	i=$((i+1)); echo "$i. CPU usage $(cmd_cpu_usage_since $s)"

	i=$((i+1)); echo "$i. Start nfqlb in the netns"
	ip netns exec ${USER}_nfqlb $me lb --vip=$__vip --lbopts="$__lbopts"

	i=$((i+1)); echo "$i. Access via nfqlb (-c $vip $xopt $@)"
	local s=$(cmd_cpu_sample)
	$iperf -c $vip $xopt $@ || die "iperf nfqlb"
	i=$((i+1)); echo "$i. CPU usage $(cmd_cpu_usage_since $s)"

	i=$((i+1)); echo "$i. Nfnetlink_queue stats"
	ip netns exec ${USER}_nfqlb $me qstats --sudo=$__sudo

	i=$((i+1)); echo "$i. Frag stats"
	ip netns exec ${USER}_nfqlb $__sudo $nfqlb stats

	i=$((i+1)); echo "$i. Stop nfqlb in the netns"
	ip netns exec ${USER}_nfqlb $me lb --vip=$__vip --stop
	
	i=$((i+1)); echo "$i. Remove route to VIP inside the netns"
	ip netns exec ${USER}_nfqlb $ip ro del $__vip via $server

	i=$((i+1)); echo "$i. Remove addresses from dev lo"
	$nfqlbsh multi_address --sudo=$__sudo --delete

	i=$((i+1)); echo "$i. Remove the route to VIP"
	$ip ro del $__vip via $gw || die "Remove route to [$__vip]"
}
cmd_lb() {
	test -n "$__vip" || die "No VIP"
	find_progs
	test -n "$__sudo" || __sudo=sudo
	local iptables="$__sudo iptables"
	echo $__vip | grep -q : && iptables="$__sudo ip6tables"

	if test "$__stop" = "yes"; then
		$__sudo killall nfqlb
		$iptables -t mangle -D PREROUTING 1
		$__sudo rm -f /dev/shm/ftshm /dev/shm/nfqlb
		return
	fi
	test -n "$__queue" || __queue=0:7
	$iptables -t mangle -A PREROUTING -d $__vip -j NFQUEUE \
		--queue-balance $__queue || die "$iptables NFQUEUE"
	$__sudo rm -f /dev/shm/ftshm /dev/shm/nfqlb
	$__sudo $nfqlb init
	$__sudo $nfqlb activate 1			# (doesn't matter what fwmark)
	$__sudo nice -n -20 $nfqlb lb --queue=$__queue $__lbopts > /dev/null &
}
##   hw_test --serverip= --vip= [--multi-src] [--nfqlb=]
##     Execute test on a machine.
##     Prerequisite; the server must be running.
cmd_hw_test() {
	test -n "$__sudo" || __sudo=sudo
	test -n "$__vip" || die "Not set --vip="
	test -n "$__serverip" || die "No server ip"
	ping -c1 -W1 -nq $__serverip > /dev/null || die "Can't ping [$__serverip]"
	find_progs
	local s i=0

	local xopt queue ip="$__sudo ip"
	if echo "$__serverip" | grep -q :; then
		xopt=-V
		ip="$__sudo ip -6"
	fi
	if test "$__multi_src" = "yes"; then
		xopt="$xopt -B $PREFIX:10.200.200.1 --incr-srcip"
		queue=--queue=0:3
		i=$((i+1)); echo "$i. Add multiple addresses to dev lo"
		$nfqlbsh multi_address --sudo=$__sudo
	fi
	if ! $ip ro get $__vip > /dev/null 2>&1; then
		i=$((i+1)); echo "$i. Setup route to $__vip"
		$ip ro replace $__vip via $__serverip
	fi
	i=$((i+1)); echo "$i. Start LB"
	$__sudo $nfqlbsh lb --path=$(dirname $nfqlb) --vip=$__vip $queue $__serverip
	i=$((i+1)); echo "$i. Iperf direct (-c $__serverip $xopt $@)"
	s=$(cmd_cpu_sample)
	$iperf -c $__serverip $xopt $@   # direct
	i=$((i+1)); echo "$i. CPU usage $(cmd_cpu_usage_since $s)"
	i=$((i+1)); echo "$i. Iperf VIP (-c $__vip $xopt $@)"
	local vip=$(echo $__vip | cut -d/ -f1)
	s=$(cmd_cpu_sample)
	$iperf -c $vip $xopt $@        # via nfqlb
	i=$((i+1)); echo "$i. CPU usage $(cmd_cpu_usage_since $s)"
	i=$((i+1)); echo "$i. Nfnetlink_queue stats"
	cmd_qstats
	i=$((i+1)); echo "$i. Stop LB"
	$__sudo $nfqlbsh stop_lb --path=$(dirname $nfqlb) --vip=$__vip $__serverip
	if test "$__multi_src" = "yes"; then
		i=$((i+1)); echo "$i. Remove addresses from dev lo"
		$nfqlbsh multi_address --sudo=$__sudo --delete
	fi
	$ip ro del $__vip via $__serverip > /dev/null 2>&1
}
find_progs() {
	iperf=$HOME/Downloads/iperf
	test -x $iperf || iperf=iperf
	nfqlb=/tmp/$USER/nfqlb/nfqlb/nfqlb
	test -x "$nfqlb"||  nfqlb=$dir/nfqlb
	test -x "$nfqlb"|| die "Not found [nfqlb]"
	nfqlbsh=${nfqlb}.sh
	test -x $nfqlbsh || nfqlbsh=$dir/nfqlb.sh
	test -x $nfqlbsh || nfqlbsh=$(readlink -f $dir/..)/nfqlb.sh
	test -x $nfqlbsh || die "Not found [nfqlb.sh]"
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
	cmd_qstats nfqlb
	i=$((i+1)); echo "$i. Re-start iperf servers"
	cmd_start_iperf_server > /dev/null 2>&1
	local vip=$(echo $__vip | cut -d/ -f1)
	i=$((i+1)); echo "$i. Iperf VIP (-c $vip $xopt $@)"
	cmd_iperf -c $vip $xopt $@
	i=$((i+1)); echo "$i. CPU usage $(cmd_cpu_usage_since $s)"
	i=$((i+1)); echo "$i. Nfnetlink_queue stats"
	cmd_qstats nfqlb
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
