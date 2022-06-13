#! /bin/sh
##
## nfqlb.sh --
##
##   Help scriptlets for;
##   https://github.com/Nordix/nfqueue-loadbalancer/
##

prg=$(basename $0)
dir=$(dirname $0); dir=$(readlink -f $dir)
tmp=/tmp/${prg}_$$
me=$dir/$prg
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


## Maintenance Commands;
##

##   include_check
##     Remove any unnecessary #include's in c-files
cmd_include_check() {
	local f
	cd $dir
	for f in $(find src -name '*.c'); do
		echo $f | grep -q extension && continue
		echo $f
		cmd_include_check_file $f > /dev/null 2>&1
	done
	for f in $(find src -name '*.c'); do
		sed -i "/\/\/#include/d" $f
	done
}
cmd_include_check_file() {
	test -n "$1" || die "No file"
	test -r "$1" || die "Not readable [$1]"
	local f=$(readlink -f $1)
	echo $f | grep -q "$dir/src" || die "File must be in src/"
	local i target
	for i in $(grep '^#include <.*>' $f | sed -E 's,^#include <(.*)>,\1,'); do
		echo $i | grep -q assert && continue
		sed -i -e "s,^#include <$i>,//#include <$i>," $f
		target=all
		echo $f | grep -q test && target=test_progs
		echo $target
		if ! make -C $dir/src CFLAGS="-DVERBOSE -DSANITY_CHECK -Werror" $target; then
			sed -i -e "s,^//#include <$i>,#include <$i>," $f
		fi
	done
}

##   add_pragma_once
##     Add "#pragma once" in header files if needed
cmd_add_pragma_once() {
	local f
	for f in $(find src -name '*.h'); do
		grep -q '^#pragma once' $f && continue
		sed -i '1s/^/#pragma once\n/' $f
		echo $f
	done
}

##   update_license [--check-only]
##     Check and update license notes
cmd_update_license() {
	local Y=$(date +%Y)
	if ! grep -q "Copyright 2021-$Y Nordix foundation" LICENSE; then
		echo "Copyright incorrect in; LICENSE"
		grep "Copyright.*Nordix" LICENSE
	fi
	local f
	for f in $(find src -name '*.[c|h]'); do
		grep -q 'SPDX-License-Identifier:' $f || continue
		if ! grep -Fq "Copyright (c) 2021-$Y Nordix Foundation" $f; then
			echo "Copyright incorrect in; $f"
			if test "$__check_only" != "yes"; then
				sed -i -e "s,(c) 2021,(c) 2021-$Y," $f
				echo "Copyright updated in; $f"
			fi
		fi
	done
}
##   mkrelease <version>
##     Create a release archive
cmd_mkrelease() {
	test -n "$1" || die "No version"
	echo "$1" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+(-.+|$)' \
		|| die "Incorrect version (semver) [$1]"
	local ver=$1
	make -C $dir/src clean
	cmd_libnfqueue_download
	__force=yes
	cmd_libmnl_unpack
	cmd_libmnl_build
	make -C $dir/src VERSION=$ver -j8 static || die make
	local dst=$tmp/nfqlb-$ver
	mkdir -p $dst/bin $dst/lib $dst/include
	local O=/tmp/$USER/nfqlb
	cp $O/nfqlb/nfqlb $O/nfqlb/ipu $dst/bin
	cp $O/lib/libnfqlb.a $dst/lib
	cp $dir/src/lib/*.h $dst/include
	cp -R $dir/src/nfqlb $dst/src
	cp $me $dst/src
	cp -R $dir/src/extension $dst/src
	cp $dir/src/README.md $dst/src
	tar -C $tmp -cf /tmp/nfqlb-$ver.tar nfqlb-$ver
	rm -f /tmp/nfqlb-$ver.tar.xz
	xz /tmp/nfqlb-$ver.tar
	echo "Created [/tmp/nfqlb-$ver.tar.xz]"
}

##
## Test/example Commands;
##

#  start_image
#    The start-point for the test container. Don't call manually!
cmd_start_image() {
	test -x $dir/nfqlb_start && exec $dir/nfqlb_start
	exec tail -f /dev/null			# Block
}

##   multi_address [--delete]
##     Setup multiple addresses on dev "lo"
cmd_multi_address() {
	local op=replace
	test "$__delete" = "yes" && op=del
	$__sudo sysctl -w net.ipv4.ip_nonlocal_bind=1 > /dev/null \
		|| die "sysctl -w net.ipv4.ip_nonlocal_bind=1"
	$__sudo sysctl -w net.ipv6.ip_nonlocal_bind=1 > /dev/null \
		|| die "sysctl -w net.ipv6.ip_nonlocal_bind=1"
	$__sudo ip addr $op 10.200.200.0/24 dev lo
	$__sudo ip -6 addr $op $PREFIX:10.200.200.0/120 dev lo
	$__sudo ip -6 ro $op local $PREFIX:10.200.200.0/120 dev lo
}

##   lb [--queue=] [--flows=n] [--lbopts] --vip=<virtual-ip> <targets...>
##     NOTE: Should normally be executed in a container.
##     Setup load-balancing to targets. Examples;
##
##       lb --vip=10.0.0.0/32 172.17.0.4 172.17.0.5 172.17.0.6
##       lb --vip=1000::/128 2000::4 2000::5 2000::6
cmd_lb() {
	test -n "$__vip" || die "No VIP address"
	local iptables=iptables
	local ip=ip
	if echo $__vip | grep -q :; then
		iptables=ip6tables
		ip="ip -6"
	fi
	local n fw fws ntargets=0
	for n in $@; do
		ntargets=$((ntargets + 1))
		fw=$((ntargets + 100))
		fws="$fws $fw"
		#$ip rule add fwmark $fw table $fw
		#$ip route add default via $n table $fw
		$iptables -t nat -A OUTPUT -m mark --mark $fw \
			-j DNAT --to-destination $n
	done
	test $ntargets -eq 0 && return 0

	test -n "$__queue" || __queue=2
	if echo $__queue | grep -q :; then
		$iptables -t mangle -A OUTPUT -d $__vip -j NFQUEUE --queue-balance $__queue
	else
		$iptables -t mangle -A OUTPUT -d $__vip -j NFQUEUE --queue-num $__queue
	fi

	test -n "$__path" || __path=/opt/nfqlb/bin
	PATH=$PATH:$__path
	nfqlb show > /dev/null 2>&1 || nfqlb init
	nfqlb activate $fws
	if test -n "$__flows"; then
		log "FLOWS are used. nflows=$__flows"
		nfqlb flowlb --queue=$__queue $__lbopts >> /var/log/nfqlb.log 2>&1 &
		sleep 0.1
		nfqlb flow-set --name=flow1 --target=nfqlb --protocols=tcp --dsts=$__vip --dport=5001
		n=2
		while test $n -le $__flows; do
			nfqlb flow-set --name=flow$n --target=nfqlb --prio=$n --protocols=tcp --dsts=$__vip --dport=$n || die flow-set
			n=$((n + 1))
		done
	else
		nfqlb lb --queue=$__queue $__lbopts >> /var/log/nfqlb.log 2>&1 &
	fi
}
##   stop_lb --vip=<virtual-ip> <targets...>
##     Stop load-balancing.
##     NOTE: must be invoked with the same parameters as "lb".
cmd_stop_lb() {
	test -n "$__vip" || die "No VIP address"
	killall nfqlb
	rm -f /dev/shm/ftshm /dev/shm/nfqlb
	local iptables=iptables
	local ip=ip
	if echo $__vip | grep -q :; then
		iptables=ip6tables
		ip="ip -6"
	fi
	local n fw ntargets=0
	for n in $@; do
		ntargets=$((ntargets + 1))
		fw=$((ntargets + 100))
		$ip rule del fwmark $fw table $fw
		$ip route del default via $n table $fw
		$iptables -t nat -D OUTPUT -m mark --mark $fw \
			-j DNAT --to-destination $n
	done

	$iptables -t mangle -D OUTPUT -d $__vip -j NFQUEUE --queue-num 2
}


##
## Build Commands;
##

##   libnfqueue_download
##   libnfqueue_unpack [--force] [--dest=]
##   libnfqueue_build [--dest=]
##     Build libnetfilter_queue locally. This is required for "make -j8 static"
##     on Ubuntu 20.04 since no static lib is included in the dev package.

netfilter_url=https://netfilter.org/projects
libnfqueue_ver=1.0.3
libnfqueue_ar=libnetfilter_queue-${libnfqueue_ver}.tar.bz2
libnfqueue_url=$netfilter_url/libnetfilter_queue/files/$libnfqueue_ar

cmd_libnfqueue_download() {
	local dstd=$ARCHIVE
	test -n "$dstd" || dstd=$HOME/Downloads
	if test -r $dstd/$libnfqueue_ar; then
		log "Already downloaded [$dstd/$libnfqueue_ar]"
		return 0
	fi
	curl -L $libnfqueue_url > $dstd/$libnfqueue_ar || die curl
}
cmd_libnfqueue_unpack() {
	local ar=$ARCHIVE/$libnfqueue_ar
	test -r $ar || ar=$HOME/Downloads/$libnfqueue_ar
	test -r $ar || die "Not readable [$ar]"
	test -n "$__dest" || __dest=/tmp/$USER/nfqlb
	local d=$__dest/libnetfilter_queue-$libnfqueue_ver
	test "$__force" = "yes" && rm -r $d
	test -d $d && die "Already unpacked [$d]"
	mkdir -p $__dest || die "mkdie $__dest"
	tar -C $__dest -xf $ar
}
cmd_libnfqueue_build() {
	test -n "$__dest" || __dest=/tmp/$USER/nfqlb
	local d=$__dest/libnetfilter_queue-$libnfqueue_ver
	test -d $d || die "Not a directory [$d]"
	cd $d
	./configure --enable-static || die configure
	make -j8 || die make
	make DESTDIR=$__dest/sys install || die "make install"
}

##   libmnl_download
##   libmnl_unpack [--force] [--dest=]
##   libmnl_build [--dest=]
##     Build libmnl locally. This is required for "make -j8 static"
##     on Ubuntu 22.04 since no static lib is included in the dev package.

libmnl_ver=1.0.4
libmnl_ar=libmnl-$libmnl_ver.tar.bz2
libmnl_url=$netfilter_url/libmnl/files/$libmnl_ar

cmd_libmnl_download() {
	local dstd=$ARCHIVE
	test -n "$dstd" || dstd=$HOME/Downloads
	if test -r $dstd/$libnfqueue_ar; then
		log "Already downloaded [$dstd/$libnfqueue_ar]"
		return 0
	fi
	curl -L $libnfqueue_url > $dstd/$libnfqueue_ar || die curl
}
cmd_libmnl_unpack() {
	local ar=$ARCHIVE/$libmnl_ar
	test -r $ar || ar=$HOME/Downloads/$libmnl_ar
	test -r $ar || die "Not readable [$ar]"
	test -n "$__dest" || __dest=/tmp/$USER/nfqlb
	local d=$__dest/libmnl-$libmnl_ver
	test "$__force" = "yes" && rm -r $d
	test -d $d && die "Already unpacked [$d]"
	mkdir -p $__dest || die "mkdie $__dest"
	tar -C $__dest -xf $ar
}
cmd_libmnl_build() {
	test -n "$__dest" || __dest=/tmp/$USER/nfqlb
	local d=$__dest/libmnl-$libmnl_ver
	test -d $d || die "Not a directory [$d]"
	cd $d
	./configure --enable-static || die configure
	make -j8 || die make
	make DESTDIR=$__dest/sys install || die "make install"
}

##   build_alpine_image
##   build_image
##     Build a docker images for test
cmd_build_image() {
	cd $dir
	local d=$dir/image/opt/nfqlb/bin
	mkdir -p $d
	cp $me $d
	make -C src clean > /dev/null
	make -C src X=$d/nfqlb || die make
	strip $d/nfqlb
	docker build -t nordixorg/nfqlb:latest .
}
cmd_build_alpine_image() {
	cd $dir
	local img=registry.nordix.org/cloud-native/nfqlb
	local d=$dir/image/opt/nfqlb/bin
	mkdir -p $d
	cp $me $d
	make -C src clean > /dev/null
	make -C src -j8 static X=$d/nfqlb || die make
	strip $d/nfqlb
	local iperf=$HOME/Downloads/iperf
	if test -r $iperf; then
		log "Using [$iperf]"
		chmod a+x $iperf
		d=$dir/image/usr/bin
		mkdir -p $d
		cp $iperf $d
	fi
	docker build -t $img:latest -f Dockerfile.alpine .
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
