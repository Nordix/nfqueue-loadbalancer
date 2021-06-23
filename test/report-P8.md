# Nfqlb test report - iperf -P8
```
1. Start iperf servers
2. Start the test container
3. Start LB
4. Iperf direct (-c 172.17.0.1  -P8)
[  6] local 172.17.0.3 port 46756 connected with 172.17.0.1 port 5001
[  5] local 172.17.0.3 port 46754 connected with 172.17.0.1 port 5001
[  3] local 172.17.0.3 port 46744 connected with 172.17.0.1 port 5001
[  4] local 172.17.0.3 port 46752 connected with 172.17.0.1 port 5001
------------------------------------------------------------
Client connecting to 172.17.0.1, TCP port 5001
TCP window size: 85.0 KByte (default)
------------------------------------------------------------
[  1] local 172.17.0.3 port 46746 connected with 172.17.0.1 port 5001
[  2] local 172.17.0.3 port 46742 connected with 172.17.0.1 port 5001
[  7] local 172.17.0.3 port 46758 connected with 172.17.0.1 port 5001
[  8] local 172.17.0.3 port 46760 connected with 172.17.0.1 port 5001
[ ID] Interval       Transfer     Bandwidth
[  3] 0.00-10.01 sec  3.75 GBytes  3.21 Gbits/sec
[  8] 0.00-10.01 sec  3.73 GBytes  3.20 Gbits/sec
[  2] 0.00-10.01 sec  3.65 GBytes  3.13 Gbits/sec
[  7] 0.00-10.01 sec  3.68 GBytes  3.15 Gbits/sec
[  1] 0.00-10.01 sec  3.64 GBytes  3.12 Gbits/sec
[  5] 0.00-10.01 sec  3.70 GBytes  3.17 Gbits/sec
[  4] 0.00-10.01 sec  3.76 GBytes  3.23 Gbits/sec
[  6] 0.00-10.01 sec  3.66 GBytes  3.14 Gbits/sec
[SUM] 0.00-10.00 sec  29.6 GBytes  25.4 Gbits/sec
[ CT] final connect times (min/avg/max/stdev) = 0.045/0.114/0.196/0.063 ms (tot/err) = 8/0
5. CPU usage 89.9%
6. Nfnetlink_queue stats
  Q       port inq cp   rng  Qdrop  Udrop      Seq
  2         85   0  2  1280      0      0        0
7. Re-start iperf servers
8. Iperf VIP (-c 10.0.0.0  -P8)
[  4] local 172.17.0.3 port 44696 connected with 10.0.0.0 port 5001
[  2] local 172.17.0.3 port 44700 connected with 10.0.0.0 port 5001
------------------------------------------------------------
Client connecting to 10.0.0.0, TCP port 5001
TCP window size: 85.0 KByte (default)
------------------------------------------------------------
[  3] local 172.17.0.3 port 44698 connected with 10.0.0.0 port 5001
[  1] local 172.17.0.3 port 44694 connected with 10.0.0.0 port 5001
[  5] local 172.17.0.3 port 44708 connected with 10.0.0.0 port 5001
[  8] local 172.17.0.3 port 44710 connected with 10.0.0.0 port 5001
[  6] local 172.17.0.3 port 44712 connected with 10.0.0.0 port 5001
[  7] local 172.17.0.3 port 44714 connected with 10.0.0.0 port 5001
[ ID] Interval       Transfer     Bandwidth
[  8] 0.00-10.03 sec   999 MBytes   835 Mbits/sec
[  5] 0.00-10.03 sec   831 MBytes   695 Mbits/sec
[  6] 0.00-10.03 sec  1012 MBytes   846 Mbits/sec
[  4] 0.00-10.03 sec   744 MBytes   622 Mbits/sec
[  7] 0.00-10.03 sec   949 MBytes   794 Mbits/sec
[  2] 0.00-10.03 sec   877 MBytes   734 Mbits/sec
[  1] 0.00-10.03 sec  1005 MBytes   841 Mbits/sec
[  3] 0.00-10.03 sec  1013 MBytes   847 Mbits/sec
[SUM] 0.00-10.01 sec  7.26 GBytes  6.23 Gbits/sec
[ CT] final connect times (min/avg/max/stdev) = 0.062/0.096/0.134/0.056 ms (tot/err) = 8/0
9. CPU usage 56.1%
10. Nfnetlink_queue stats
  Q       port inq cp   rng  Qdrop  Udrop      Seq
  2         85   0  2  1280      0      0   120700
11. Stop the container
```
