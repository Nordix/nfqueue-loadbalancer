# Nfqlb test report - iperf -P8
```
1. Start iperf servers
2. Start the test container
3. Start LB
4. Iperf direct (-c 172.17.0.1 -P8)
------------------------------------------------------------
Client connecting to 172.17.0.1, TCP port 5001
TCP window size: 85.0 KByte (default)
------------------------------------------------------------
[  2] local 172.17.0.3 port 55166 connected with 172.17.0.1 port 5001
[  1] local 172.17.0.3 port 55168 connected with 172.17.0.1 port 5001
[  4] local 172.17.0.3 port 55172 connected with 172.17.0.1 port 5001
[  5] local 172.17.0.3 port 55174 connected with 172.17.0.1 port 5001
[  3] local 172.17.0.3 port 55170 connected with 172.17.0.1 port 5001
[  6] local 172.17.0.3 port 55176 connected with 172.17.0.1 port 5001
[  7] local 172.17.0.3 port 55178 connected with 172.17.0.1 port 5001
[  8] local 172.17.0.3 port 55180 connected with 172.17.0.1 port 5001
[ ID] Interval       Transfer     Bandwidth
[  8] 0.00-10.00 sec  11.3 GBytes  9.68 Gbits/sec
[  2] 0.00-10.00 sec  11.2 GBytes  9.65 Gbits/sec
[  5] 0.00-10.00 sec  11.6 GBytes  9.97 Gbits/sec
[  1] 0.00-10.00 sec  11.0 GBytes  9.42 Gbits/sec
[  6] 0.00-10.00 sec  11.5 GBytes  9.86 Gbits/sec
[  4] 0.00-10.00 sec  11.2 GBytes  9.62 Gbits/sec
[  7] 0.00-10.00 sec  11.4 GBytes  9.81 Gbits/sec
[  3] 0.00-10.00 sec  11.3 GBytes  9.74 Gbits/sec
[SUM] 0.00-10.00 sec  90.5 GBytes  77.7 Gbits/sec
[ CT] final connect times (min/avg/max/stdev) = 0.144/0.221/0.407/0.100 ms (tot/err) = 8/0
5. CPU usage 89.5%
6. Nfnetlink_queue stats
  Q       port inq cp   rng  Qdrop  Udrop      Seq
  0 2827892614   0  2  1280      0      0        0
  1 2836442906   0  2  1280      0      0        0
  2 3201286572   0  2  1280      0      0        0
  3         71   0  2  1280      0      0        0
7. Re-start iperf servers
8. Iperf VIP (-c 10.0.0.0 -P8)
------------------------------------------------------------
Client connecting to 10.0.0.0, TCP port 5001
TCP window size: 85.0 KByte (default)
------------------------------------------------------------
[  1] local 172.17.0.3 port 45406 connected with 10.0.0.0 port 5001
[  2] local 172.17.0.3 port 45410 connected with 10.0.0.0 port 5001
[  4] local 172.17.0.3 port 45404 connected with 10.0.0.0 port 5001
[  3] local 172.17.0.3 port 45408 connected with 10.0.0.0 port 5001
[  5] local 172.17.0.3 port 45414 connected with 10.0.0.0 port 5001
[  7] local 172.17.0.3 port 45416 connected with 10.0.0.0 port 5001
[  6] local 172.17.0.3 port 45412 connected with 10.0.0.0 port 5001
[  8] local 172.17.0.3 port 45418 connected with 10.0.0.0 port 5001
[ ID] Interval       Transfer     Bandwidth
[  8] 0.00-10.00 sec  5.11 GBytes  4.39 Gbits/sec
[  3] 0.00-10.00 sec  5.44 GBytes  4.67 Gbits/sec
[  4] 0.00-10.00 sec  6.07 GBytes  5.22 Gbits/sec
[  2] 0.00-10.00 sec  5.49 GBytes  4.72 Gbits/sec
[  6] 0.00-10.00 sec  5.03 GBytes  4.32 Gbits/sec
[  1] 0.00-10.00 sec  5.72 GBytes  4.91 Gbits/sec
[  7] 0.00-10.00 sec  5.49 GBytes  4.71 Gbits/sec
[  5] 0.00-10.00 sec  5.81 GBytes  4.99 Gbits/sec
[SUM] 0.00-10.00 sec  44.2 GBytes  37.9 Gbits/sec
[ CT] final connect times (min/avg/max/stdev) = 0.040/0.133/0.439/0.138 ms (tot/err) = 8/0
9. CPU usage 62.6%
10. Nfnetlink_queue stats
  Q       port inq cp   rng  Qdrop  Udrop      Seq
  0 2827892614   0  2  1280      0      0        0
  1 2836442906   0  2  1280      0      0        0
  2 3201286572   0  2  1280      0   7949   796469
  3         71   0  2  1280      0      0        0
10. Stop the container
```
