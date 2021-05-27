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

##  env
##    Print environment.
##
cmd_env() {
	test "$cmd" = "env" && set | grep -E '^(__.*|ARCHIVE)='
}

##  include_check
##    Remove any unnecessary #include in c-files
##
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
		sed -i -e "s,^#include <$i>,//#include <$i>," $f
		target=all
		echo $f | grep -q test && target=test_progs
		echo $target
		if ! make -C $dir/src CFLAGS=-Werror $target; then
			sed -i -e "s,^//#include <$i>,#include <$i>," $f
		fi
	done
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
