# Nfqlb test report - iperf -b2G -u
```
1. Start iperf servers
2. Start the test container
3. Start LB
4. Iperf direct (-c 172.17.0.1 -b2G -u)
------------------------------------------------------------
Client connecting to 172.17.0.1, UDP port 5001
Sending 1470 byte datagrams, IPG target: 5.48 us (kalman adjust)
UDP buffer size:  208 KByte (default)
------------------------------------------------------------
[  1] local 172.17.0.3 port 33254 connected with 172.17.0.1 port 5001
ñ[ ID] Interval       Transfer     Bandwidth
[  1] 0.00-10.00 sec  2.50 GBytes  2.15 Gbits/sec
[  1] Sent 1826091 datagrams
[  1] Server Report:
[ ID] Interval       Transfer     Bandwidth        Jitter   Lost/Total Datagrams
[  1] 0.00-0.00 sec  0.000 Bytes  -nan bits/sec  131.072 ms 4294966296/0 (inf%)
5. CPU usage 12.4%
6. Nfnetlink_queue stats
  Q       port inq cp   rng  Qdrop  Udrop      Seq
  2         71   0  2  1280      0      0        0
7. Re-start iperf servers
8. Iperf VIP (-c 10.0.0.0 -b2G -u)
------------------------------------------------------------
Client connecting to 10.0.0.0, UDP port 5001
Sending 1470 byte datagrams, IPG target: 5.48 us (kalman adjust)
UDP buffer size:  208 KByte (default)
------------------------------------------------------------
[  1] local 172.17.0.3 port 44999 connected with 10.0.0.0 port 5001
[ ID] Interval       Transfer     Bandwidth
[  1] 0.00-10.00 sec  2.27 GBytes  1.95 Gbits/sec
[  1] Sent 1655426 datagrams
[  1] Server Report:
[ ID] Interval       Transfer     Bandwidth        Jitter   Lost/Total Datagrams
[  1] 0.00-0.00 sec  0.000 Bytes  -nan bits/sec  131.072 ms 4294966296/0 (inf%)
9. CPU usage 17.9%
10. Nfnetlink_queue stats
  Q       port inq cp   rng  Qdrop  Udrop      Seq
  2         71   0  2  1280      0      0  1655426
10. Stop the container
```
