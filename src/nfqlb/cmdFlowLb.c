/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021-2022 Nordix Foundation
*/

#include "nfqlb.h"

#include <nfqueue.h>
#include <shmem.h>
#include <cmd.h>
#include <tuntap.h>
#include <maglevdyn.h>
#include <reassembler.h>
#include <flow.h>
#include <log.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <arpa/inet.h>

#ifdef VERBOSE
#define D(x)
#define Dx(x) x
#else
#define D(x)
#define Dx(x)
#endif

#ifdef UNIT_TEST
// White-box testing
#define STATIC
#else
#define STATIC static
#endif

STATIC void cmd_set(struct FlowCmd* cmd, int cd);
STATIC void cmd_delete(struct FlowCmd* cmd, int cd);

#define REFINC(x) __atomic_add_fetch(&(x),1,__ATOMIC_SEQ_CST)
#define REFDEC(x) __atomic_sub_fetch(&(x),1,__ATOMIC_SEQ_CST)
#define LOCK(m) pthread_mutex_lock(&(m))
#define UNLOCK(m) pthread_mutex_unlock(&(m))
#define MALLOC(x) calloc(1, sizeof(*(x))); if (x == NULL) die("OOM")


// Types
struct LoadBalancer {
	struct LoadBalancer* next;
	int refCounter;
	char* target;
	int fd;
	struct SharedData* st;
	struct MagDataDyn magd;
};

// Forward declarations;
static void* flowThread(void* a);
STATIC void loadbalancerLock(void* user_ref);
STATIC void loadbalancerRelease(struct LoadBalancer* lb);
static void traceHandleFlowCmd(struct FlowCmd* cmd, FILE* out, int cd);

// Statics
static struct FragTable* ft;
static int tun_fd = -1;
static struct fragStats* sft;
static struct SharedData* slb;
static struct MagDataDyn magdlb;
STATIC struct FlowSet* fset;
static struct FlowSet* trace_fset;
STATIC struct LoadBalancer* lblist = NULL;
static pthread_mutex_t lblistLock = PTHREAD_MUTEX_INITIALIZER;
static int notargets_fw = -1;
static int nolb_fw = -1;

static void injectFrag(void const* data, unsigned len)
{
	if (tun_fd >= 0) {
		int rc = write(tun_fd, data, len);
		trace(TRACE_FRAG, "Frag injected, len=%u, rc=%d\n", len, rc);
		if (rc != len)
			warning("FAILED: injectFrag write(%u), rc=%d\n", len, rc);
	} else {
		trace(TRACE_FRAG, "Frag dropped, len=%u\n", len);
	}
}

static int packetHandleFn(
	unsigned short proto, void* data, unsigned len)
{
	struct ctKey key;
	uint64_t fragid;
	int rc = getHashKey(&key, 0, &fragid, proto, data, len);
	if (rc < 0) {
		warning("getHashKey rc=%d. proto=%u, len=%u\n", rc, proto, len);
		return -1;
	}

	unsigned hash;
	int fw;
	if (rc & 3) {
		// Fragment. Check if we shall forward to the lb-tier
		if (slb != NULL) {
			hash = hashKeyAddresses(&key);
			fw = magdlb.lookup[hash % magdlb.M];
			if (fw >= 0)
				fw = magdlb.active[fw];
			if (fw >= 0 && fw != slb->ownFwmark) {
				trace(TRACE_FRAG, "Fragment to LB tier. fw=%d\n", fw);
				return fw; /* To the LB tier */
			}
		}
		// We shall handle the fragment here
		if ((rc & 1) == 0) {
			// Not first-fragment
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			rc = fragGetValueOrStore(ft, &now, &key, &fw, data, len);
			if (rc != 0) {
				trace(TRACE_FRAG, "Fragment %s\n", rc > 0 ? "stored":"dropped");
				return -1;
			}
			trace(TRACE_FRAG, "Handle non-first frag locally fwmark=%d\n", fw);
			return fw;
		}
	}

	unsigned short udpencap = 0;
	struct LoadBalancer* lb = flowLookup(
		fset, &key, proto, data, len, &udpencap);
	// (NOTE: the received lb is locked. Call loadbalancerRelease(lb))

	char const* tflow = NULL;
	TRACE(TRACE_FLOWS) {
		tflow = flowLookup(trace_fset, &key, proto, data, len, NULL);
		if (tflow != NULL) {
			tracef("\nMatch for trace-flow: %s\n", tflow);
			printIcmp(tracef, proto, data, len); //(will be a no-op if not icmp)
			char src[INET6_ADDRSTRLEN];
			char dst[INET6_ADDRSTRLEN];
			tracef(
				"proto=%s, len=%u, %s %u -> %s %u\n",
				protostr(key.ports.proto, NULL), len,
				inet_ntop(AF_INET6, &key.src, src, sizeof(src)),
				ntohs(key.ports.src),
				inet_ntop(AF_INET6, &key.dst, dst, sizeof(dst)),
				ntohs(key.ports.dst));
			if (key.ports.proto == IPPROTO_UDP && udpencap != 0) {
				tracef("udpencap=%u\n", udpencap);
			}
		}
	}
	
	if (key.ports.proto == IPPROTO_UDP && ntohs(key.ports.dst) == udpencap) {
		/*
		  We have an udp encapsulated sctp packet. Re-compute the key
		  and make a new lookup. (note; lb==NULL here)
		 */
		trace(TRACE_SCTP,"Udp encapsulated sctp packet on port %u\n", udpencap);
		rc = getHashKey(&key, udpencap, &fragid, proto, data, len);
		if (rc < 0) {
			// (this shouldn't happen)
			trace(TRACE_SCTP, "FAILED: Re-compute key with udpencap\n");
			warning("FAILED: Re-compute key with udpencap\n");
			return -1;
		}
		lb = flowLookup(fset, &key, proto, data, len, NULL);
		if (lb == NULL)
			trace(TRACE_SCTP, "Failed flowLookup for udpencap\n");
	}

	if (lb == NULL) {
		trace(TRACE_PACKET, "Failed flowLookup\n");
		info("Failed flowLookup\n");
		if (tflow != NULL)
			tracef("NO target, fwmark=%d\n", nolb_fw);
		return nolb_fw;
	}

	// Compute the fwmark
	hash = hashKey(&key);
	fw = lb->magd.lookup[hash % lb->magd.M];
	if (fw >= 0)
		fw = lb->magd.active[fw];
	loadbalancerRelease(lb);
	if (fw < 0) {
		if (tflow != NULL)
			tracef(
				"NO servers, target=%s, fwmark=%d\n", lb->target, notargets_fw);
		return notargets_fw;
	}
	if (tflow != NULL) {
		tracef("target=%s, fwmark=%d\n", lb->target, fw);
	}

	if (rc & 1) {
		// First fragment
		trace(TRACE_FRAG, "First fragment\n");
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		key.id = fragid;
		if (handleFirstFragment(ft, &now, &key, fw, data, len) != 0) {
			trace(TRACE_FRAG, "FAILED: Handle first fragment\n");
			if (tflow != NULL)
				tracef("FAILED: Handle first fragment\n");
			return -1;
		}
	}

	TRACE(TRACE_PACKET|TRACE_SCTP){
		TRACE(TRACE_PACKET) {
			tracef("Using LB; %s\n", lb->target);
			tracef(
				"Packet; proto=%u, len=%u, fwmark=%u\n",
				key.ports.proto, len, fw);
		} else {
			if (key.ports.proto == IPPROTO_SCTP) {
				tracef("Using LB; %s\n", lb->target);
				tracef(
					"Packet; proto=%u, len=%u, fwmark=%u\n",
					key.ports.proto, len, fw);
			}
		}
	}
	return fw;
}

static void* packetHandleThread(void* Q)
{
	nfqueueRun((intptr_t)Q);
	return NULL;
}

static int cmdFlowLb(int argc, char **argv)
{
	char const* lbShm = NULL;
	char const* qnum = "2";
	char const* qlen = "1024";
	char const* ftShm = "ftshm";
	char const* ft_size = "500";
	char const* ft_buckets = "500";
	char const* ft_frag = "100";
	char const* ft_ttl = "200";
	char const* mtuOpt = "1500";
	char const* tun = NULL;
	char const* reassembler = "0";
	char const* promiscuous_ping = "no";
	char const* notargets_fwmark = "-1";
	char const* nolb_fwmark = "-1";
	char const* trace_address = DEFAULT_TRACE_ADDRESS;
	struct Option options[] = {
		{"help", NULL, 0,
		 "flowlb [options]\n"
		 "  Load-balance with flows"},
		{"lbshm", &lbShm, 0, "Lb shared memory"},
		{"mtu", &mtuOpt, 0, "MTU. At least the mtu of the ingress device"},
		{"tun", &tun, 0, "Tun device for re-inject fragments"},
		{"reassembler", &reassembler, 0, "Reassembler size. default=0"},
		{"promiscuous_ping", &promiscuous_ping, 0,
		 "Accept ping on any flow with an address match"},
		{"notargets_fwmark", &notargets_fwmark, 0, "Set when there are no targets"},
		{"nolb_fwmark", &nolb_fwmark, 0, "Set when there is no matching LB"},
		{"queue", &qnum, 0, "NF-queues to listen to (default 2)"},
		{"qlength", &qlen, 0, "Lenght of queues (default 1024)"},
		{"ft_shm", &ftShm, 0, "Frag table; shared memory stats"},
		{"ft_size", &ft_size, 0, "Frag table; size"},
		{"ft_buckets", &ft_buckets, 0, "Frag table; extra buckets"},
		{"ft_frag", &ft_frag, 0, "Frag table; stored frags"},
		{"ft_ttl", &ft_ttl, 0, "Frag table; ttl milliS"},
		{"trace_address",  &trace_address, 0, "Trace server address"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);
	logConfigShm(TRACE_SHM);
	logTraceServer(trace_address);

	if (lbShm != NULL) {
		slb = mapSharedDataOrDie(lbShm, O_RDONLY, NULL);
		magDataDyn_map(&magdlb, slb->mem);
	}

	trace_fset = flowSetCreate(NULL);
	flowSetPromiscuousPing(trace_fset, 1);

	fset = flowSetCreate(loadbalancerLock);
	if (promiscuous_ping == NULL)
		flowSetPromiscuousPing(fset, 1);
	notargets_fw = atoi(notargets_fwmark);
	nolb_fw = atoi(nolb_fwmark);

	// Create and re-map the stats struct
	sft = calloc(1, sizeof(*sft));
	createSharedDataOrDie(ftShm, sft, sizeof(*sft));
	free(sft);
	sft = mapSharedDataOrDie(ftShm, O_RDWR, NULL);

	// Get MTU from the ingress device
	int mtu = atoi(mtuOpt);
	if (mtu < 576)
		die("Invalid MTU; %d\n", mtu);

	/* Open the "tun" device if specified. Check that the mtu is at
	 * least as large as for the ingress device */
	if (tun != NULL) {
		tun_fd = tun_alloc(tun, IFF_TUN|IFF_NO_PI);
		if (tun_fd < 0)
			die("Failed to open tun device [%s]\n", tun);
		int tun_mtu = get_mtu(tun);
		if (tun_mtu < mtu)
			die("Tun mtu too small; %d < %d\n", tun_mtu, mtu);
		setInjectFn(injectFrag);
	} else {
		/*
		  We can't inject stored fragments. Disable storing of
		  fragments. The MTU *may* be set to to 1280 to avoid copy unnecessary
		  data to user-space, but then reassembly will not work since the
		  original packet lenght is lost.
		 */
		ft_frag = "0";
	}

	ft = fragTableCreate(
		atoi(ft_size),		/* table size */
		atoi(ft_buckets),	/* Extra buckets for hash collisions */
		atoi(ft_frag),		/* Max stored fragments */
		mtu,				/* MTU. Only used for stored fragments */
		atoi(ft_ttl));		/* Fragment TTL in milli seconds */
	fragUseStats(ft, sft);
	if (atoi(reassembler) > 0)
		fragRegisterFragReassembler(ft, createReassembler(atoi(reassembler)));
	printf(
		"FragTable; size=%d, buckets=%d, frag=%d, mtu=%d, ttl=%d\n",
		atoi(ft_size),atoi(ft_buckets),atoi(ft_frag),mtu,atoi(ft_ttl));

	nfqueueInit(packetHandleFn, atoi(qlen), mtu);

	pthread_t tid;
	if (pthread_create(&tid, NULL, flowThread, NULL) != 0)
		die("Failed pthread_create for flow\n");
	
	
	/*
	  The qnum may be a range like "0:3" in which case we go
	  multi-threading and listen to all queues in the range;
	 */
	unsigned Q = atoi(qnum);
	if (strchr(qnum, ':') != NULL) {
		unsigned first, last;
		if (sscanf(qnum, "%u:%u", &first, &last) != 2)
			die("queue invalid [%s]\n", qnum);
		if (first > last)
			die("queue invalid [%s]\n", qnum);
		for (Q = first; Q < last; Q++) {
			if (pthread_create(&tid, NULL, packetHandleThread, (void*)(intptr_t)Q) != 0)
				die("Failed pthread_create for Q=%u\n", Q);
		}
		Q = last;				/* Last will go to the main thread */
	}
	return nfqueueRun(Q);
}

__attribute__ ((__constructor__)) static void addCommands(void) {
	addCmd("flowlb", cmdFlowLb);
}

/* ----------------------------------------------------------------------
   LoadBalancer handling;
 */

STATIC void loadbalancerLock(void* user_ref)
{
	struct LoadBalancer* lb = user_ref;
	REFINC(lb->refCounter);
}
STATIC void loadbalancerRelease(struct LoadBalancer* lb)
{
	if (lb == NULL)
		return;

	int refCounter = REFDEC(lb->refCounter);
	assert(refCounter >= 0);
	if (refCounter == 0) {
		trace(TRACE_TARGET, "Delete load-balancer; %s\n", lb->target);
		LOCK(lblistLock);
		// Unlink the lb from the list.
		if (lblist == lb)
			lblist = lb->next;
		else {
			struct LoadBalancer* item;
			for (item = lblist; item != NULL; item = item->next) {
				if (item->next == lb) {
					item->next = lb->next;
					break;
				}
			}
		}
		UNLOCK(lblistLock);

		// Unmap shm and free
		struct stat statbuf;
		if (fstat(lb->fd, &statbuf) != 0)
			die("fstat shared mem; %s\n", lb->target);
		munmap(lb->st, statbuf.st_size);
		close(lb->fd);
		free(lb->target);
		free(lb);
	}
}

STATIC struct LoadBalancer* loadbalancerFindOrCreate(char const* target)
{
	if (target == NULL)
		return NULL;

	LOCK(lblistLock);

	// Search for it
	struct LoadBalancer* lb;
	for (lb = lblist; lb != NULL; lb = lb->next) {
		if (strcmp(lb->target, target) == 0) {
			REFINC(lb->refCounter);
			UNLOCK(lblistLock);
			trace(TRACE_TARGET, "Found LB; %s (%u)\n", target, lb->refCounter);
			return lb;
		}
	}

	// Not found, create a new LB
	trace(TRACE_TARGET, "Creating LB; %s\n", target);
	int fd;
	size_t len;
	struct SharedData* st = mapSharedDataRead(target, &fd, &len);
	if (st == NULL) {
		UNLOCK(lblistLock);
		trace(TRACE_TARGET, "Map shm failed; %s\n", target);
		return NULL;
	}
	if (magDataDyn_validate(st->mem, len - sizeof(struct SharedData)) != 0) {
		UNLOCK(lblistLock);
		trace(TRACE_TARGET, "Shm invalid; %s\n", target);
		return NULL;
	}


	lb = MALLOC(lb);
	lb->target = strdup(target);
	if (lb->target == NULL) die("OOM");
	lb->refCounter = 1;
	lb->fd = fd;
	lb->st = st;
	magDataDyn_map(&lb->magd, st->mem);

	lb->next = lblist;
	lblist = lb;

	UNLOCK(lblistLock);
	return lb;
}



/* ----------------------------------------------------------------------
   Handle incoming flow commands;
   TODO: Use grpc or something... but grpc has no support for C.
 */

static void writeReply(int fd, char const* msg)
{
	trace(TRACE_FLOW_CONF, "writeReply [%s]\n", msg);
	unsigned len = strlen(msg) + 1;
	write(fd, msg, len);
}

static char const* lb2string(void* user_ref)
{
	if (user_ref == NULL)
		return "NULL";
	struct LoadBalancer* lb = user_ref;
	return lb->target;
}

static void* flowThread(void* a)
{
	struct sockaddr_storage sa;
	socklen_t len;
	char const* addr;
	addr = getenv("NFQLB_FLOW_ADDRESS");
	if (addr == NULL)
		addr = DEFAULT_FLOW_ADDRESS;
	if (parseAddress(addr, &sa, &len) != 0)
		die("Failed to parse address [%s]", addr);
	int sd = socket(sa.ss_family, SOCK_STREAM, 0);
	if (sd < 0)
		die("Flow server socket: %s\n", strerror(errno));
	if (bind(sd, (struct sockaddr*)&sa, len) != 0)
		die("Flow server bind: %s\n", strerror(errno));
	if (listen(sd, 128) != 0)
		die("Flow server listen: %s\n", strerror(errno));
	FILE* in = NULL;
	FILE* out = NULL;
	struct FlowCmd cmd = {0};
	for (;;) {
		// Cleanup
		if (in != NULL)
			fclose(in);
		if (out != NULL)
			fclose(out);
		in = out = NULL;

		int cd = accept(sd, NULL, NULL);
		debug("flowThread: Accepted incoming connection. cd=%d\n", cd);
		if (cd < 0) {
			warning("flowThread: accept returns %d\n", cd);
			continue;
		}
		in = fdopen(cd, "r");
		if (in == NULL) {
			warning("flowThread: fdopen failed\n");
			close(cd);
			continue;
		}

		freeFlowCmd(&cmd);
		if (readFlowCmd(in, &cmd) != 0)
			continue;
		if (cmd.action == NULL) {
			writeReply(cd, "FAIL: no action");
			continue;
		}
		if (strncmp(cmd.action, "trace-", 6) == 0) {
			out = fdopen(dup(cd), "w");
			if (out == NULL)
				writeReply(cd, "FAIL: fdopen");
			else
				traceHandleFlowCmd(&cmd, out, cd);
			continue;
		}

		if (strcmp(cmd.action, "set") == 0) {
			cmd_set(&cmd, cd);
		} else if (strcmp(cmd.action, "delete") == 0) {
			cmd_delete(&cmd, cd);
		} else if (strcmp(cmd.action, "list") == 0) {
			out = fdopen(dup(cd), "w");
			if (out == NULL)
				writeReply(cd, "FAIL: fdopen");
			else {
				D(flowSetPrint(stdout, fset, cmd.name, lb2string));
				flowSetPrint(out, fset, cmd.name, lb2string);
				fflush(out);
			}
		} else if (strcmp(cmd.action, "list-names") == 0) {
			out = fdopen(dup(cd), "w");
			if (out == NULL)
				writeReply(cd, "FAIL: fdopen");
			else {
				D(flowSetPrint(stdout, fset, cmd.name, lb2string));
				flowSetPrintNames(out, fset);
				fflush(out);
			}
		} else {
			writeReply(cd, "FAIL: action unknown");
		}
	}
	return NULL;
}

STATIC void cmd_set(struct FlowCmd* cmd, int cd)
{
	char const* err;
	struct LoadBalancer* lb;
	unsigned short udpencap;

	lb = flowLookupName(fset, cmd->name, &udpencap);
	if (lb != NULL) {
		// The flow exists already.
		loadbalancerRelease(lb); /* We already have reserved the lb */
		if (udpencap != cmd->udpencap) {
			writeReply(cd, "FAIL: Alter udpencap not allowed");
			return;
		}
		if (strcmp(cmd->target, lb->target) != 0) {
			struct LoadBalancer* newLb = loadbalancerFindOrCreate(cmd->target);
			if (newLb == NULL) {
				writeReply(cd, "FAIL: Couldn't create new load-balancer");
				return;
			}
			loadbalancerRelease(lb);
			lb = newLb;
		}
	} else {
		lb = loadbalancerFindOrCreate(cmd->target);
	}
	if (lb == NULL) {
		writeReply(cd, "FAIL: Couldn't create load-balancer");
		return;
	}

	if (cmd->udpencap != 0) {
		/*
		  If a UDP encapsulated SCTP port is defined the
		  protocols must be "sctp" only.
		*/
		if (cmd->protocols == NULL || cmd->protocols[1] != NULL ||
			strcasecmp(cmd->protocols[0], "sctp") != 0) {
			writeReply(cd, "FAIL: only sctp for updencap");
			return;
		}
		/*
		  The defined set will never match since the
		  incoming encapsulated sctp will actually be a
		  udp packet with dport=udpencap. So we must
		  insert a flow for the encapsulating udp also.
		*/
		char udpname[MAX_CMD_LINE];
		udpname[0] = '#';
		strncpy(udpname+1, cmd->name, MAX_CMD_LINE-2);
		const char* udpproto[] = {"udp", NULL};
		char udpdport[16];
		sprintf(udpdport, "%u", cmd->udpencap);
		err = flowDefine(
			fset, udpname, cmd->priority, NULL, udpproto, udpdport,
			NULL, cmd->dsts, cmd->srcs, NULL, cmd->udpencap);
		if (err != NULL) {
			writeReply(cd, err);
			return;
		}
	}

	err = flowDefine(
		fset, cmd->name, cmd->priority, lb, cmd->protocols,
		cmd->dports, cmd->sports, cmd->dsts, cmd->srcs,
		cmd->match, cmd->udpencap);
	if (err == NULL)
		writeReply(cd, "OK");
	else
		writeReply(cd, err);
}

STATIC void cmd_delete(struct FlowCmd* cmd, int cd)
{
	if (cmd->name != NULL) {
		unsigned short udpencap = 0;
		loadbalancerRelease(flowDelete(fset, cmd->name, &udpencap));
		if (udpencap > 0) {
			/*
			  This is a sctp flow with encapsulater udp. We
			  have a associated udp flow that must also be
			  deleted. The udp flow has not reserved the loadbalancer.
			*/
			char udpname[MAX_CMD_LINE];
			udpname[0] = '#';
			strncpy(udpname+1, cmd->name, MAX_CMD_LINE-2);
			flowDelete(fset, udpname, NULL);
		}
		writeReply(cd, "OK");
	} else {
		writeReply(cd, "FAIL: no name");
	}
}

static void traceHandleFlowCmd(struct FlowCmd* cmd, FILE* out, int cd)
{
	if (strcmp(cmd->action, "trace-set") == 0) {
		char const* err;
		void* user_ref = strdup(cmd->name);
		if (user_ref == NULL)
			die("OOM");
		err = flowDefine(
			trace_fset, cmd->name, cmd->priority, user_ref, cmd->protocols,
			cmd->dports, cmd->sports, cmd->dsts, cmd->srcs,
			cmd->match, cmd->udpencap);
		if (err != NULL) {
			writeReply(cd, err);
		}
	} else if (strcmp(cmd->action, "trace-delete") == 0) {
		if (cmd->name != NULL) {
			void* user_ref = flowDelete(trace_fset, cmd->name, NULL);
			free(user_ref);
			writeReply(cd, "OK");
		} else {
			writeReply(cd, "FAIL: no name");
		}
	} else if (strcmp(cmd->action, "trace-list") == 0) {
		flowSetPrint(out, trace_fset, cmd->name, NULL);
		fflush(out);
	} else if (strcmp(cmd->action, "trace-list-names") == 0) {
		flowSetPrintNames(out, trace_fset);
		fflush(out);
	} else {
		writeReply(cd, "FAIL: action unknown");
	}
}
