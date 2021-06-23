# Nfqlb test report - iperf -P8
```
1. Start iperf servers
2. Start the test container
3. Add multiple addresses in the container
4. Add routes to multi-addresses
5. Start LB
6. Iperf direct (-c 172.17.0.1 -B 10.200.200.1 --incr-srcip -P8)
------------------------------------------------------------
Client connecting to 172.17.0.1, TCP port 5001
TCP window size: 85.0 KByte (default)
------------------------------------------------------------
[  3] local 10.200.200.3 port 36821 connected with 172.17.0.1 port 5001
[  2] local 10.200.200.2 port 60681 connected with 172.17.0.1 port 5001
[  1] local 10.200.200.1 port 38157 connected with 172.17.0.1 port 5001
[  4] local 10.200.200.4 port 58363 connected with 172.17.0.1 port 5001
[  5] local 10.200.200.7 port 40433 connected with 172.17.0.1 port 5001
[  6] local 10.200.200.8 port 39137 connected with 172.17.0.1 port 5001
[  8] local 10.200.200.6 port 56487 connected with 172.17.0.1 port 5001
[  7] local 10.200.200.5 port 49885 connected with 172.17.0.1 port 5001
[ ID] Interval       Transfer     Bandwidth
[  5] 0.00-10.01 sec  4.00 GBytes  3.43 Gbits/sec
[  4] 0.00-10.01 sec  4.05 GBytes  3.48 Gbits/sec
[  6] 0.00-10.01 sec  4.03 GBytes  3.46 Gbits/sec
[  8] 0.00-10.01 sec  4.03 GBytes  3.46 Gbits/sec
[  1] 0.00-10.01 sec  3.99 GBytes  3.42 Gbits/sec
[  7] 0.00-10.01 sec  3.95 GBytes  3.39 Gbits/sec
[  2] 0.00-10.01 sec  3.97 GBytes  3.40 Gbits/sec
[  3] 0.00-10.01 sec  4.02 GBytes  3.45 Gbits/sec
[SUM] 0.00-10.00 sec  32.0 GBytes  27.5 Gbits/sec
[ CT] final connect times (min/avg/max/stdev) = 0.096/0.200/0.369/0.143 ms (tot/err) = 8/0
7. CPU usage 89.6%
8. Nfnetlink_queue stats
  Q       port inq cp   rng  Qdrop  Udrop      Seq
  0        110   0  2  1280      0      0        0
  1 2257893141   0  2  1280      0      0        0
  2 3982443105   0  2  1280      0      0        0
  3 2297496563   0  2  1280      0      0        0
  4 4192946525   0  2  1280      0      0        0
  5 4096974014   0  2  1280      0      0        0
  6 3460775992   0  2  1280      0      0        0
  7 4011903053   0  2  1280      0      0        0
9. Re-start iperf servers
10. Iperf VIP (-c 10.0.0.0 -B 10.200.200.1 --incr-srcip -P8)
[  3] local 10.200.200.2 port 50255 connected with 10.0.0.0 port 5001
------------------------------------------------------------
Client connecting to 10.0.0.0, TCP port 5001
TCP window size: 85.0 KByte (default)
------------------------------------------------------------
[  4] local 10.200.200.5 port 58395 connected with 10.0.0.0 port 5001
[  2] local 10.200.200.3 port 59547 connected with 10.0.0.0 port 5001
[  1] local 10.200.200.1 port 51301 connected with 10.0.0.0 port 5001
[  6] local 10.200.200.8 port 35669 connected with 10.0.0.0 port 5001
[  5] local 10.200.200.7 port 52853 connected with 10.0.0.0 port 5001
[  7] local 10.200.200.4 port 43195 connected with 10.0.0.0 port 5001
[  8] local 10.200.200.6 port 41329 connected with 10.0.0.0 port 5001
[ ID] Interval       Transfer     Bandwidth
[  8] 0.00-10.01 sec  3.48 GBytes  2.99 Gbits/sec
[  7] 0.00-10.01 sec  3.59 GBytes  3.08 Gbits/sec
[  6] 0.00-10.01 sec  3.44 GBytes  2.95 Gbits/sec
[  5] 0.00-10.03 sec  1.58 GBytes  1.35 Gbits/sec
[  3] 0.00-10.03 sec  1.51 GBytes  1.29 Gbits/sec
[  1] 0.00-10.03 sec  1.16 GBytes   996 Mbits/sec
[  2] 0.00-10.04 sec  1.52 GBytes  1.30 Gbits/sec
[  4] 0.00-10.03 sec  2.25 GBytes  1.92 Gbits/sec
[SUM] 0.00-10.01 sec  18.5 GBytes  15.9 Gbits/sec
[ CT] final connect times (min/avg/max/stdev) = 0.064/0.107/0.160/0.043 ms (tot/err) = 8/0
11. CPU usage 84.4%
12. Nfnetlink_queue stats
  Q       port inq cp   rng  Qdrop  Udrop      Seq
  0        110   0  2  1280      0      0    56782
  1 2257893141   0  2  1280      0      0    57565
  2 3982443105   0  2  1280      0      0        0
  3 2297496563   0  2  1280      0      0        0
  4 4192946525   0  2  1280      0      0    69440
  5 4096974014   0  2  1280      0      0        0
  6 3460775992   0  2  1280      0      0    59274
  7 4011903053   0  2  1280      0      0    63211
13. Remove routes to multi-addresses
14. Stop the container
```
