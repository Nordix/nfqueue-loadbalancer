# Nfqlb test report - iperf -P8
```
1. Start iperf servers
2. Rebuild the test container
3. Start the test container
4. Add multiple addresses in the container
5. Add routes to multi-addresses
6. Start LB
7. Iperf direct (-c 172.17.0.1 -B 10.200.200.1 --incr-srcip -P8)
[  4] local 10.200.200.5 port 46463 connected with 172.17.0.1 port 5001
[  7] local 10.200.200.3 port 53695 connected with 172.17.0.1 port 5001
[  1] local 10.200.200.2 port 37079 connected with 172.17.0.1 port 5001
[  3] local 10.200.200.4 port 40429 connected with 172.17.0.1 port 5001
[  5] local 10.200.200.6 port 52589 connected with 172.17.0.1 port 5001
[  2] local 10.200.200.1 port 46911 connected with 172.17.0.1 port 5001
------------------------------------------------------------
Client connecting to 172.17.0.1, TCP port 5001
TCP window size: 85.0 KByte (default)
------------------------------------------------------------
[  6] local 10.200.200.7 port 49401 connected with 172.17.0.1 port 5001
[  8] local 10.200.200.8 port 56881 connected with 172.17.0.1 port 5001
[ ID] Interval       Transfer     Bandwidth
[  2] 0.00-10.01 sec  11.4 GBytes  9.79 Gbits/sec
[  5] 0.00-10.01 sec  11.9 GBytes  10.2 Gbits/sec
[  8] 0.00-10.01 sec  11.7 GBytes  10.1 Gbits/sec
[  4] 0.00-10.01 sec  11.8 GBytes  10.1 Gbits/sec
[  3] 0.00-10.01 sec  11.5 GBytes  9.86 Gbits/sec
[  7] 0.00-10.01 sec  11.6 GBytes  9.97 Gbits/sec
[  6] 0.00-10.01 sec  11.9 GBytes  10.2 Gbits/sec
[  1] 0.00-10.01 sec  11.7 GBytes  10.0 Gbits/sec
[SUM] 0.00-10.00 sec  93.5 GBytes  80.3 Gbits/sec
[ CT] final connect times (min/avg/max/stdev) = 0.079/0.187/0.380/0.115 ms (tot/err) = 8/0
8. CPU usage 89.2%
9. Nfnetlink_queue stats
  Q       port inq cp   rng  Qdrop  Udrop      Seq
  0         94   0  2  1280      0      0        0
  1 4200449521   0  2  1280      0      0        0
  2 3724718350   0  2  1280      0      0        0
  3 2801016281   0  2  1280      0      0        0
10. Re-start iperf servers
11. Iperf VIP (-c 10.0.0.0 -B 10.200.200.1 --incr-srcip -P8)
------------------------------------------------------------
Client connecting to 10.0.0.0, TCP port 5001
TCP window size: 85.0 KByte (default)
------------------------------------------------------------
[  3] local 10.200.200.5 port 53013 connected with 10.0.0.0 port 5001
[  1] local 10.200.200.1 port 51261 connected with 10.0.0.0 port 5001
[  2] local 10.200.200.3 port 38831 connected with 10.0.0.0 port 5001
[  4] local 10.200.200.7 port 49297 connected with 10.0.0.0 port 5001
[  6] local 10.200.200.6 port 53373 connected with 10.0.0.0 port 5001
[  5] local 10.200.200.4 port 40347 connected with 10.0.0.0 port 5001
[  7] local 10.200.200.8 port 58979 connected with 10.0.0.0 port 5001
[  8] local 10.200.200.2 port 38751 connected with 10.0.0.0 port 5001
[ ID] Interval       Transfer     Bandwidth
[  5] 0.00-10.02 sec  8.91 GBytes  7.64 Gbits/sec
[  2] 0.00-10.02 sec  13.7 GBytes  11.7 Gbits/sec
[  4] 0.00-10.02 sec  9.60 GBytes  8.23 Gbits/sec
[  7] 0.00-10.02 sec  9.24 GBytes  7.93 Gbits/sec
[  3] 0.00-10.02 sec  9.15 GBytes  7.85 Gbits/sec
[  6] 0.00-10.02 sec  11.8 GBytes  10.1 Gbits/sec
[  8] 0.00-10.02 sec  11.7 GBytes  10.0 Gbits/sec
[  1] 0.00-10.02 sec  9.22 GBytes  7.91 Gbits/sec
[SUM] 0.00-10.00 sec  83.3 GBytes  71.5 Gbits/sec
[ CT] final connect times (min/avg/max/stdev) = 0.078/0.109/0.166/0.071 ms (tot/err) = 8/0
12. CPU usage 82.5%
13. Nfnetlink_queue stats
  Q       port inq cp   rng  Qdrop  Udrop      Seq
  0         94   0  2  1280      0      0   461041
  1 4200449521   0  2  1280      0      0   362172
  2 3724718350   0  2  1280      0      0   368951
  3 2801016281   0  2  1280      0      0   246858
14. Remove routes to multi-addresses
15. Stop the container
```
