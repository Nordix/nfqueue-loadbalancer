# Nfqlb with SCTP

Describes problems and solutions for load-balaning with multihomed
SCTP.

It essential that all paths of an association ends up in the same
backend (server).

<img src="sctp-lb.svg" alt="SCTP multihomed load-balancing" width="70%" />

For a multihomed SCTP association the addresses (both source and dest)
are different for the paths but *the ports are always the same*. So
for SCTP the `nfqlb` hash on ports only.

The properties, such as scalability, are the same as for tcp/udp but
distribution will basically be based the on source port (since the
dest (server) port is the same).

