#include "nfqueue.h"

/* ----------------------------------------------------------------------
   The NFQUEUE code is taken from the example in;
   libnetfilter_queue-1.0.3/examples/nf-queue.c
   http://www.netfilter.org/projects/libnetfilter_queue/doxygen/
*/

#include <libmnl/libmnl.h>
#include <linux/netfilter.h>
#include <libnetfilter_queue/libnetfilter_queue.h>
/* only for NFQA_CT, not needed otherwise:
#include <linux/netfilter/nfnetlink_conntrack.h>
*/
#include <stdlib.h>

static packetHandleFn_t handlePacket = NULL;
static unsigned queue_length = 1024;
static unsigned mtu = 1500;

void nfqueueInit(
	packetHandleFn_t packetHandleFn, unsigned _queue_length, unsigned _mtu)
{
	handlePacket = packetHandleFn;
	queue_length = _queue_length;
	mtu = _mtu;
}


static struct nlmsghdr *
nfq_hdr_put(char *buf, int type, uint32_t queue_num)
{
	struct nlmsghdr *nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type	= (NFNL_SUBSYS_QUEUE << 8) | type;
	nlh->nlmsg_flags = NLM_F_REQUEST;

	struct nfgenmsg *nfg = mnl_nlmsg_put_extra_header(nlh, sizeof(*nfg));
	nfg->nfgen_family = AF_UNSPEC;
	nfg->version = NFNETLINK_V0;
	nfg->res_id = htons(queue_num);

	return nlh;
}

static void
nfq_send_verdict(
	struct mnl_socket *nl, int queue_num, uint32_t id,
	uint32_t mark, uint32_t verdict)
{
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;

	nlh = nfq_hdr_put(buf, NFQNL_MSG_VERDICT, queue_num);
	nfq_nlmsg_verdict_put(nlh, id, verdict);
	nfq_nlmsg_verdict_put_mark(nlh, mark);
#if 0
	struct nlattr *nest;
	/* example to set the connmark. First, start NFQA_CT section: */
	nest = mnl_attr_nest_start(nlh, NFQA_CT);

	/* then, add the connmark attribute: */
	mnl_attr_put_u32(nlh, CTA_MARK, htonl(42));
	/* more conntrack attributes, e.g. CTA_LABEL, could be set here */

	/* end conntrack section */
	mnl_attr_nest_end(nlh, nest);
#endif
	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
		perror("mnl_socket_send");
		exit(EXIT_FAILURE);
	}
}

static int queue_cb(const struct nlmsghdr *nlh, void *data)
{
	struct nfqnl_msg_packet_hdr *ph = NULL;
	struct nlattr *attr[NFQA_MAX+1] = {};
	uint32_t id = 0;
	struct nfgenmsg *nfg;
	uint16_t plen;

	if (nfq_nlmsg_parse(nlh, attr) < 0) {
		perror("problems parsing");
		return MNL_CB_ERROR;
	}

	nfg = mnl_nlmsg_get_payload(nlh);

	if (attr[NFQA_PACKET_HDR] == NULL) {
		fputs("metaheader not set\n", stderr);
		return MNL_CB_ERROR;
	}

	ph = mnl_attr_get_payload(attr[NFQA_PACKET_HDR]);

	plen = mnl_attr_get_payload_len(attr[NFQA_PAYLOAD]);
	id = ntohl(ph->packet_id);

	uint8_t *payload = mnl_attr_get_payload(attr[NFQA_PAYLOAD]);
	int fwmark = handlePacket(ntohs(ph->hw_protocol), payload, plen);
	if (fwmark < 0) 
		nfq_send_verdict(data, ntohs(nfg->res_id), id, 0, NF_DROP);
	else
		nfq_send_verdict(data, ntohs(nfg->res_id), id, fwmark, NF_ACCEPT);

	return MNL_CB_OK;
}

int nfqueueRun(unsigned int queue_num)
{
	char *buf;
	/* largest copied packet payload, plus netlink data overhead: */
	size_t sizeof_buf = mtu + (MNL_SOCKET_BUFFER_SIZE/2);
	unsigned int portid;
	int ret;
	struct nlmsghdr *nlh;
	struct mnl_socket *nl;

	if (handlePacket == NULL)
		exit(EXIT_FAILURE);

	nl = mnl_socket_open(NETLINK_NETFILTER);
	if (nl == NULL) {
		perror("mnl_socket_open");
		exit(EXIT_FAILURE);
	}

	if (mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID) < 0) {
		perror("mnl_socket_bind");
		exit(EXIT_FAILURE);
	}
	portid = mnl_socket_get_portid(nl);
	buf = malloc(sizeof_buf);
	if (!buf) {
		perror("allocate receive buffer");
		exit(EXIT_FAILURE);
	}

	/* PF_(UN)BIND is not needed with kernels 3.8 and later */
	nlh = nfq_hdr_put(buf, NFQNL_MSG_CONFIG, 0);
	nfq_nlmsg_cfg_put_cmd(nlh, AF_INET, NFQNL_CFG_CMD_PF_UNBIND);

	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
		perror("mnl_socket_send");
		exit(EXIT_FAILURE);
	}

	nlh = nfq_hdr_put(buf, NFQNL_MSG_CONFIG, 0);
	nfq_nlmsg_cfg_put_cmd(nlh, AF_INET, NFQNL_CFG_CMD_PF_BIND);

	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
		perror("mnl_socket_send");
		exit(EXIT_FAILURE);
	}

	nlh = nfq_hdr_put(buf, NFQNL_MSG_CONFIG, queue_num);
	nfq_nlmsg_cfg_put_cmd(nlh, AF_INET, NFQNL_CFG_CMD_BIND);

	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
		perror("mnl_socket_send");
		exit(EXIT_FAILURE);
	}

	nlh = nfq_hdr_put(buf, NFQNL_MSG_CONFIG, queue_num);
	nfq_nlmsg_cfg_put_params(nlh, NFQNL_COPY_PACKET, mtu);

	mnl_attr_put_u32(nlh, NFQA_CFG_FLAGS, htonl(NFQA_CFG_F_GSO));
	mnl_attr_put_u32(nlh, NFQA_CFG_MASK, htonl(NFQA_CFG_F_GSO));

	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
		perror("mnl_socket_send");
		exit(EXIT_FAILURE);
	}

	nlh = nfq_hdr_put(buf, NFQNL_MSG_CONFIG, queue_num);
	nfq_nlmsg_cfg_put_qmaxlen(nlh, queue_length);
	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
		perror("mnl_socket_send");
		exit(EXIT_FAILURE);
	}

	/* ENOBUFS is signalled to userspace when packets were lost
	 * on kernel side.  In most cases, userspace isn't interested
	 * in this information, so turn it off. */
	ret = 1;
	mnl_socket_setsockopt(nl, NETLINK_NO_ENOBUFS, &ret, sizeof(int));

	/*
	  nfnl_rcvbufsiz() mentioned in the docs seem obsolete, but
	  checking the source in libnfnetlink-1.0.1/src/libnfnetlink.c it
	  just calls setsockopt().
	  cat /proc/sys/net/ipv4/tcp_rmem # min default max
	 */
	int n = 0;
	int fd = mnl_socket_get_fd(nl);
	socklen_t socklen = sizeof(n);
	if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &n, &socklen) == 0) {
		int newsize = (queue_length * mtu / 2) & ~0xfff;
		if (newsize > n) {
			setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &newsize, socklen);
		}
		getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &n, &socklen);
		printf(
			"queue_length=%u, mtu=%u, SO_RCVBUF=%u (%u)\n",
			queue_length, mtu, n, newsize);
	} else {
		printf("getsockopt failed\n");
	}

	for (;;) {
		ret = mnl_socket_recvfrom(nl, buf, sizeof_buf);
		if (ret == -1) {
			perror("mnl_socket_recvfrom");
			exit(EXIT_FAILURE);
		}
		ret = mnl_cb_run(buf, ret, 0, portid, queue_cb, nl);
		if (ret < 0){
			perror("mnl_cb_run");
			exit(EXIT_FAILURE);
		}
	}

	/* We will never get here */
	mnl_socket_close(nl);
	return 0;
}
