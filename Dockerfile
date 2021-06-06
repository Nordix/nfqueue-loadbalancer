FROM ubuntu:20.04
RUN \
  apt update; \
  apt install -y libmnl0:amd64 libnetfilter-queue1 iproute2 iptables netcat tcpdump iputils-ping iperf
COPY --chown=0:0 image/ /
CMD ["/opt/nfqlb/bin/nfqlb.sh", "start_image"]
