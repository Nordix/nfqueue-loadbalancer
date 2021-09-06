# nfqueue-loadbalancer - extend or alter

The [nfqueue-loadbalancer](https://github.com/Nordix/nfqueue-loadbalancer/)
release includes a development environment that let you extend or alter the
`nfqlb`.

To build the `nfqlb` binary;
```
#sudo apt install -y libmnl-dev libnetfilter-queue-dev
export NFQLB_DIR=/path/to/nfqlb-x.y.z  # the release directory
cd $NFQLB_DIR/src
make help
make -j8
```

## New commands

New commands can be added to `nfqlb` without altering the `nfqlb`
code. This allows you to maintain a modified `nfqlb` outside, and
independent of, the `nfqlb` repository.

A new command must add itself using a "constructor";

```c
__attribute__ ((__constructor__)) static void addCommands(void) {
        addCmd("xlb", cmdXlb);
}
```

The constructor is executed before `main()` (similar to static
constructors in C++).

The modified `nfqlb` then can be built with;
```
gcc -o mynfqlb -pthread -I$NFQLB_DIR/include cmdXlb.c $NFQLB_DIR/src/*.c \
  -L$NFQLB_DIR/lib -lnfqlb -lmnl -lnetfilter_queue -lrt
```

An example is included in `src/extension`;
```
cd $NFQLB_DIR/src/extension
make help
make -j8
```


## Static build

On `Ubuntu` static libraries for `netfilter_queue` are not included so
the source must be downloaded and built locally;
```
cd $NFQLB_DIR/src
./nfqlb.sh libnfqueue_download
./nfqlb.sh libnfqueue_unpack --dest=/tmp/$USER/nfqlb
./nfqlb.sh libnfqueue_build --dest=/tmp/$USER/nfqlb
```

Now build a statically linked binary with;
```
export NFQD=/tmp/$USER/nfqlb/libnetfilter_queue-1.0.3/sys/usr/local
cd $NFQLB_DIR/src/extension
#make clean # (remove any lingering dynamic build)
make -j8 static
```
