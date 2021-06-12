# Nfqlb test report - iperf -P8
```
1. Start iperf servers
2. Rebuild the test container
3. Start the test container
4. Add multiple addresses in the container
5. Add routes to multi-addresses
6. Start LB
7. Iperf direct (-c 172.17.0.1 -B 10.200.200.1 --incr-srcip -P8)
------------------------------------------------------------
Client connecting to 172.17.0.1, TCP port 5001
TCP window size: 85.0 KByte (default)
------------------------------------------------------------
[  5] local 10.200.200.7 port 38383 connected with 172.17.0.1 port 5001
[  4] local 10.200.200.3 port 36695 connected with 172.17.0.1 port 5001
[  2] local 10.200.200.1 port 43405 connected with 172.17.0.1 port 5001
[  3] local 10.200.200.4 port 59637 connected with 172.17.0.1 port 5001
[  6] local 10.200.200.6 port 49513 connected with 172.17.0.1 port 5001
[  7] local 10.200.200.5 port 36593 connected with 172.17.0.1 port 5001
[  1] local 10.200.200.2 port 49115 connected with 172.17.0.1 port 5001
[  8] local 10.200.200.8 port 39305 connected with 172.17.0.1 port 5001
[ ID] Interval       Transfer     Bandwidth
[  1] 0.00-10.00 sec  11.5 GBytes  9.88 Gbits/sec
[  7] 0.00-10.00 sec  11.4 GBytes  9.82 Gbits/sec
[  6] 0.00-10.00 sec  11.5 GBytes  9.92 Gbits/sec
[  4] 0.00-10.00 sec  11.8 GBytes  10.1 Gbits/sec
[  3] 0.00-10.00 sec  11.7 GBytes  10.0 Gbits/sec
[  2] 0.00-10.00 sec  11.8 GBytes  10.1 Gbits/sec
[  5] 0.00-10.00 sec  11.6 GBytes  9.95 Gbits/sec
[  8] 0.00-10.00 sec  11.6 GBytes  9.94 Gbits/sec
[SUM] 0.00-10.00 sec  92.9 GBytes  79.8 Gbits/sec
[ CT] final connect times (min/avg/max/stdev) = 0.038/0.160/0.805/0.265 ms (tot/err) = 8/0
8. CPU usage 89.4%
9. Nfnetlink_queue stats
  Q       port inq cp   rng  Qdrop  Udrop      Seq
  0         95   0  2  1280      0      0        0
  1 4004489124   0  2  1280      0      0        0
  2 2308128380   0  2  1280      0      0        0
  3 3690085897   0  2  1280      0      0        0
10. Re-start iperf servers
11. Iperf VIP (-c 10.0.0.0 -B 10.200.200.1 --incr-srcip -P8)
[  2] local 10.200.200.2 port 38969 connected with 10.0.0.0 port 5001
[  5] local 10.200.200.7 port 54897 connected with 10.0.0.0 port 5001
[  3] local 10.200.200.3 port 54637 connected with 10.0.0.0 port 5001
[  1] local 10.200.200.1 port 35065 connected with 10.0.0.0 port 5001
[  6] local 10.200.200.4 port 55565 connected with 10.0.0.0 port 5001
[  4] local 10.200.200.6 port 35605 connected with 10.0.0.0 port 5001
------------------------------------------------------------
Client connecting to 10.0.0.0, TCP port 5001
TCP window size: 85.0 KByte (default)
------------------------------------------------------------
[  7] local 10.200.200.5 port 56099 connected with 10.0.0.0 port 5001
[  8] local 10.200.200.8 port 55677 connected with 10.0.0.0 port 5001
[ ID] Interval       Transfer     Bandwidth
[  7] 0.00-10.01 sec  9.13 GBytes  7.84 Gbits/sec
[  2] 0.00-10.01 sec  10.3 GBytes  8.81 Gbits/sec
[  6] 0.00-10.01 sec  9.36 GBytes  8.03 Gbits/sec
[  4] 0.00-10.01 sec  10.0 GBytes  8.61 Gbits/sec
[  8] 0.00-10.01 sec  9.38 GBytes  8.05 Gbits/sec
[  3] 0.00-10.01 sec  15.3 GBytes  13.1 Gbits/sec
[  5] 0.00-10.01 sec  10.0 GBytes  8.62 Gbits/sec
[  1] 0.00-10.03 sec  9.95 GBytes  8.52 Gbits/sec
[SUM] 0.00-10.01 sec  83.4 GBytes  71.6 Gbits/sec
[ CT] final connect times (min/avg/max/stdev) = 0.163/0.334/0.639/0.178 ms (tot/err) = 8/0
12. CPU usage 86.2%
13. Nfnetlink_queue stats
  Q       port inq cp   rng  Qdrop  Udrop      Seq
  0         95   0  2  1280      0      0   472843
  1 4004489124   0  2  1280      0      0   345873
  2 2308128380   0  2  1280      0      0   351795
  3 3690085897   0  2  1280      0      0   277674
14. Remove routes to multi-addresses
14. Stop the container
```
