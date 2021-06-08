# Nfqlb test report - iperf -b4G -u
```
1. Start iperf servers
2. Start the test container
3. Start LB
4. Iperf direct (-c 172.17.0.1 -b4G -u)
------------------------------------------------------------
Client connecting to 172.17.0.1, UDP port 5001
Sending 1470 byte datagrams, IPG target: 2.74 us (kalman adjust)
UDP buffer size:  208 KByte (default)
------------------------------------------------------------
[  1] local 172.17.0.3 port 60155 connected with 172.17.0.1 port 5001
[ ID] Interval       Transfer     Bandwidth
[  1] 0.00-10.00 sec  3.81 GBytes  3.27 Gbits/sec
[  1] Sent 2781856 datagrams
[  1] Server Report:
[ ID] Interval       Transfer     Bandwidth        Jitter   Lost/Total Datagrams
[  1] 0.00-0.00 sec  0.000 Bytes  -nan bits/sec  131.072 ms 4294966296/0 (inf%)
5. CPU usage 19.6%
6. Nfnetlink_queue stats
  Q       port inq cp   rng  Qdrop  Udrop      Seq
  2         71   0  2  1280      0      0        0
7. Re-start iperf servers
8. Iperf VIP (-c 10.0.0.0 -b4G -u)
------------------------------------------------------------
Client connecting to 10.0.0.0, UDP port 5001
Sending 1470 byte datagrams, IPG target: 2.74 us (kalman adjust)
UDP buffer size:  208 KByte (default)
------------------------------------------------------------
[  1] local 172.17.0.3 port 52067 connected with 10.0.0.0 port 5001
[ ID] Interval       Transfer     Bandwidth
[  1] 0.00-10.00 sec  2.31 GBytes  1.98 Gbits/sec
[  1] Sent 1686228 datagrams
[  1] Server Report:
[ ID] Interval       Transfer     Bandwidth        Jitter   Lost/Total Datagrams
[  1] 0.00-0.00 sec  0.000 Bytes  -nan bits/sec  131.072 ms 4294966296/0 (inf%)
9. CPU usage 20.9%
10. Nfnetlink_queue stats
  Q       port inq cp   rng  Qdrop  Udrop      Seq
  2         71   0  2  1280      0      0  1686228
10. Stop the container
```
