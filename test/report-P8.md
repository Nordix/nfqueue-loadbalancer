# Nfqlb test report - iperf -P8
```
1. Start iperf servers
2. Start the test container
3. Start LB
4. Iperf direct (-c 172.17.0.1 -P8)
[  2] local 172.17.0.3 port 55128 connected with 172.17.0.1 port 5001
------------------------------------------------------------
Client connecting to 172.17.0.1, TCP port 5001
TCP window size: 85.0 KByte (default)
------------------------------------------------------------
[  5] local 172.17.0.3 port 55132 connected with 172.17.0.1 port 5001
[  3] local 172.17.0.3 port 55136 connected with 172.17.0.1 port 5001
[  1] local 172.17.0.3 port 55130 connected with 172.17.0.1 port 5001
[  4] local 172.17.0.3 port 55138 connected with 172.17.0.1 port 5001
[  6] local 172.17.0.3 port 55140 connected with 172.17.0.1 port 5001
[  7] local 172.17.0.3 port 55142 connected with 172.17.0.1 port 5001
[  8] local 172.17.0.3 port 55144 connected with 172.17.0.1 port 5001
[ ID] Interval       Transfer     Bandwidth
[  5] 0.00-10.00 sec  11.7 GBytes  10.0 Gbits/sec
[  8] 0.00-10.00 sec  11.7 GBytes  10.1 Gbits/sec
[  3] 0.00-10.00 sec  11.5 GBytes  9.90 Gbits/sec
[  4] 0.00-10.00 sec  12.0 GBytes  10.3 Gbits/sec
[  6] 0.00-10.00 sec  11.5 GBytes  9.90 Gbits/sec
[  2] 0.00-10.00 sec  11.7 GBytes  10.1 Gbits/sec
[  1] 0.00-10.00 sec  11.8 GBytes  10.1 Gbits/sec
[  7] 0.00-10.00 sec  11.8 GBytes  10.1 Gbits/sec
[SUM] 0.00-10.00 sec  93.7 GBytes  80.5 Gbits/sec
[ CT] final connect times (min/avg/max/stdev) = 0.074/0.106/0.177/0.041 ms (tot/err) = 8/0
5. CPU usage 88.9%
6. Nfnetlink_queue stats
  Q       port inq cp   rng  Qdrop  Udrop      Seq
  2         71   0  2  1280      0      0        0
7. Re-start iperf servers
8. Iperf VIP (-c 10.0.0.0 -P8)
------------------------------------------------------------
Client connecting to 10.0.0.0, TCP port 5001
TCP window size: 85.0 KByte (default)
------------------------------------------------------------
[  1] local 172.17.0.3 port 45372 connected with 10.0.0.0 port 5001
[  4] local 172.17.0.3 port 45376 connected with 10.0.0.0 port 5001
[  2] local 172.17.0.3 port 45378 connected with 10.0.0.0 port 5001
[  5] local 172.17.0.3 port 45380 connected with 10.0.0.0 port 5001
[  6] local 172.17.0.3 port 45382 connected with 10.0.0.0 port 5001
[  3] local 172.17.0.3 port 45374 connected with 10.0.0.0 port 5001
[  7] local 172.17.0.3 port 45384 connected with 10.0.0.0 port 5001
[  8] local 172.17.0.3 port 45386 connected with 10.0.0.0 port 5001
[ ID] Interval       Transfer     Bandwidth
[  8] 0.00-10.00 sec  6.01 GBytes  5.16 Gbits/sec
[  5] 0.00-10.00 sec  4.80 GBytes  4.12 Gbits/sec
[  1] 0.00-10.00 sec  5.42 GBytes  4.66 Gbits/sec
[  3] 0.00-10.00 sec  6.39 GBytes  5.49 Gbits/sec
[  2] 0.00-10.00 sec  5.66 GBytes  4.86 Gbits/sec
[  6] 0.00-10.00 sec  5.90 GBytes  5.07 Gbits/sec
[  4] 0.00-10.00 sec  5.92 GBytes  5.08 Gbits/sec
[  7] 0.00-10.00 sec  5.65 GBytes  4.85 Gbits/sec
[SUM] 0.00-10.00 sec  45.7 GBytes  39.3 Gbits/sec
[ CT] final connect times (min/avg/max/stdev) = 0.048/0.103/0.173/0.048 ms (tot/err) = 8/0
9. CPU usage 62.9%
10. Nfnetlink_queue stats
  Q       port inq cp   rng  Qdrop  Udrop      Seq
  2         71   0  2  1280      0   5928   818677
10. Stop the container
```
