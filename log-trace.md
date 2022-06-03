# Nordix/nfqueue-loadbalancer - Log and trace

Log and trace is supported but affects performance and should be used
with care.


## Log

Logs printouts are printed to `stderr`.

Log levels;

* `WARNING` (4) - Important events
* `NOTICE` (5) - Notable events (default)
* `INFO` (6) - Informative events
* `DEBUG` (7) - Debug events

If traffic is working there should not be any per-packet loggings even
on `debug` level.

The loglevel is stored in shared memory which make it possible to set
it in runtime with the `nfqlb loglevel` command;

```
# nfqlb loglevel
loglevel=5
# nfqlb loglevel 7
loglevel=7
# nfqlb loglevel
loglevel=7
```


## Trace

Trace is independent of the loglevel, that is trace is not loglevel>7.
Trace is based on a 32-bit "trace mask". Each bit represent a trace
area;

* `log` (0) - Log printouts are copied to trace printouts
* `packet` (1) - Per-packet printouts. Use with care!
* `frag` (2) - Fragment tracing. Per-fragment printouts
* `flow-conf` (3) - Flow configuration printouts
* `sctp` (4) - Per-packet sctp printouts
* `target` (5) - Trace flow target add/release (not per-packet)


To initiate trace you attach to a running `nfqlb` load-balancer with
`nfqlb trace`. Printouts goes to `stdout`;

```
nfqlb trace --selection=log,packet,frag
# Or;
nfqlb trace --mask=0xffffffff   # trace everything
```


## Trace-flows

**Not yet implemented** (but shoudn't be to hard)

Per-packet printouts should normally be avoided but may sometimes be
necessary. The flows in `nfqlb` are re-use to select packets to
trace. The flow commands are duplicated, like;

```
  trace-flow-set
  trace-flow-delete
  trace-flow-list
```

And per-packet printouts only for packets that matches the trace-flows
can be initated with;

```
nfqlb trace --selection=flows,...
```


## Compile without log/trace

There is always a fear that the even if nothing is logged or traced
the numerous checks in the code will affect performance. For tests
log/trace can be disabled;

```
make clean
CFLAGS=-DNO_LOG make -j8
```
