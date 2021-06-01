#! /bin/sh
##
## nfqlb.sh --
##
##   Help scriptlets for;
##   https://github.com/Nordix/nfqueue-loadbalancer/
##
## Commands;
##

prg=$(basename $0)
dir=$(dirname $0); dir=$(readlink -f $dir)
tmp=/tmp/${prg}_$$
me=$dir/$prg

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

#  env
#    Print environment.
#
cmd_env() {
	test "$cmd" = "env" && set | grep -E '^(__.*|ARCHIVE)='
}

##  start_image
##    The start-point for the test container. Don't call manually!
cmd_start_image() {
	test -x $dir/nfqlb_start && exec $dir/nfqlb_start
	exec tail -f /dev/null			# Block
}

##  lb --vip=<virtual-ip> <targets...>
##
##    NOTE: Should normally be executed in a container.  fwoffset=100
##          for targets is assumed (default).
##
##    Setup load-balancing to targets. Examples;
##
##      lb --vip=10.0.0.0/32 172.17.0.4 172.17.0.5 172.17.0.6
##      lb --vip=1000::/128 2000::4 2000::5 2000::6
cmd_lb() {
	test -n "$__vip" || die "No VIP address"
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
		$ip rule add fwmark $fw table $fw
		$ip route add default via $n table $fw
		$iptables -t nat -A OUTPUT -m mark --mark $fw \
			-j DNAT --to-destination $n
	done
	test $ntargets -eq 0 && return 0

	$iptables -t mangle -A OUTPUT -d $__vip -j NFQUEUE --queue-num 2

	PATH=$PATH:/opt/nfqlb/bin
	nfqlb show > /dev/null 2>&1 || nfqlb init --ownfw=1
	nfqlb activate $(seq 1 $ntargets)
	nfqlb lb >> /var/log/nfqlb.log 2>&1 &
}
##

##  libnfqueue_download
##  libnfqueue_unpack [--force] [--dest=]
##  libnfqueue_build [--dest=]
##    Build libnetfilter_queue locally. This is required for "make -j8 static"
##    on Ubuntu since no static lib is included in the dev package.
libnfqueue_ver=1.0.3
libnfqueue_ar=libnetfilter_queue-${libnfqueue_ver}.tar.bz2
libnfqueue_url=https://www.netfilter.org/projects/libnetfilter_queue/files/$libnfqueue_ar

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
	make DESTDIR=$d/sys install || die "make install"
}
##

##  include_check
##    Remove any unnecessary #include's in c-files
cmd_include_check() {
	local f
	cd $dir
	for f in $(find src -name '*.c'); do
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
		if ! make -C $dir/src CFLAGS=-Werror $target; then
			sed -i -e "s,^//#include <$i>,#include <$i>," $f
		fi
	done
}

##  add_pragma_once
##    Add "#pragma once" in header files if needed
cmd_add_pragma_once() {
	local f
	for f in $(find src -name '*.h'); do
		grep -q '^#pragma once' $f && continue
		sed -i '1s/^/#pragma once\n/' $f
		echo $f
	done
}

##  update_license
##    Check and update license notes
cmd_update_license() {
	local Y=$(date +%Y)
	if ! grep -q "Copyright $Y Nordix foundation" LICENSE; then
		echo "Copyright incorrect in; LICENSE"
		grep "Copyright.*Nordix" LICENSE
	fi
	local f
	for f in $(find src -name '*.[c|h]'); do
		if ! grep -q 'SPDX-License-Identifier:' $f; then
			echo "No SPDX-License-Identifier in; $f" >&2
			continue
		fi
		grep -q 'SPDX-License-Identifier: Apache-2.0' $f && continue
		sed -i -e 's,SPDX-License-Identifier: .*,SPDX-License-Identifier: Apache-2.0,' $f
		echo "SPDX-License-Identifier: updated in; $f"
	done
	for f in $(find src -name '*.[c|h]'); do
		grep -q 'SPDX-License-Identifier:' $f || continue
		if ! grep -q "Copyright .*$Y Nordix Foundation" $f; then
			echo "Copyright incorrect in; $f"
		fi
	done
}
##

##  build_image
##    Build a docker image for test
cmd_build_image() {
	cd $dir
	local d=$dir/image/opt/nfqlb/bin
	mkdir -p $d
	cp $me $d
	make -C src X=$d/nfqlb
	strip $d/nfqlb
	docker build -t nordixorg/nfqlb:latest .
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
