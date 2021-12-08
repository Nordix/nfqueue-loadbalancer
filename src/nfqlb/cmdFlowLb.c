/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

#include "nfqueue.h"
#include <iputils.h>
#include <shmem.h>
#include <cmd.h>
#include <die.h>
#include <tuntap.h>
#include <maglevdyn.h>
#include <reassembler.h>
#include <flow.h>
#include <argv.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/un.h>

#ifdef VERBOSE
#define D(x)
#define Dx(x) x
#else
#define D(x)
#define Dx(x)
#endif

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
static void loadbalancerLock(void* user_ref);
static void loadbalancerRelease(struct LoadBalancer* lb);

// Statics
static struct FragTable* ft;
static int tun_fd = -1;
static struct fragStats* sft;
static struct SharedData* slb;
static struct MagDataDyn magdlb;
static struct FlowSet* fset;
static struct LoadBalancer* lblist = NULL;
static pthread_mutex_t lblistLock = PTHREAD_MUTEX_INITIALIZER;


static void injectFrag(void const* data, unsigned len)
{
#ifdef VERBOSE
	if (tun_fd >= 0) {
		int rc = write(tun_fd, data, len);
		printf("Frag injected, len=%u, rc=%d\n", len, rc);
	} else {
		printf("Frag dropped, len=%u\n", len);
	}
#else
	if (tun_fd >= 0)
		write(tun_fd, data, len);
#endif
}

static int packetHandleFn(
	unsigned short proto, void* data, unsigned len)
{
	struct ctKey key;
	uint64_t fragid;
	int rc = getHashKey(&key, 0, &fragid, proto, data, len);
	if (rc < 0)
		return -1;

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
				Dx(printf("Fragment to LB tier. fw=%d\n", fw));
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
				Dx(printf("Fragment %s\n", rc > 0 ? "stored":"dropped"));
				return -1;
			}
			Dx(printf("Handle frag locally fwmark=%d\n", fw));
			return fw;
		}
	}

	unsigned short udpencap = 0;
	struct LoadBalancer* lb = flowLookup(fset, &key, &udpencap);
	// (NOTE: the received lb is locked. Call loadbalancerRelease(lb))

	D(printf("Proto=%u, dport=%u, udpencap=%u\n",
			 key.ports.proto, ntohs(key.ports.dst), udpencap));
	if (key.ports.proto == IPPROTO_UDP && ntohs(key.ports.dst) == udpencap) {
		/*
		  We have an udp encapsulated sctp packet. Re-compute the key
		  and make a new lookup.
		 */
		Dx(printf("Udp encapsulated sctp packet on %u\n", udpencap));
		rc = getHashKey(&key, udpencap, &fragid, proto, data, len);
		if (rc < 0) {
			// (this shouldn't happen)
			loadbalancerRelease(lb);
			return -1;
		}
		lb = flowLookup(fset, &key, NULL);
	}

	if (lb == NULL) {
		Dx(printf("Failed flowLookup\n"));
		return -1;
	}
	Dx(printf("Using LB; %s\n", lb->target));

	// Compute the fwmark
	hash = hashKey(&key);
	fw = lb->magd.lookup[hash % lb->magd.M];
	if (fw >= 0)
		fw = lb->magd.active[fw];
	loadbalancerRelease(lb);
	if (fw < 0)
		return -1;

	if (rc & 1) {
		// First fragment
		Dx(printf("First fragment\n"));
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		key.id = fragid;
		if (handleFirstFragment(ft, &now, &key, fw, data, len) != 0)
			return -1;
	}
	Dx(printf("packetHandleFn; fw=%d\n", fw));
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
		{"queue", &qnum, 0, "NF-queues to listen to (default 2)"},
		{"qlength", &qlen, 0, "Lenght of queues (default 1024)"},
		{"ft_shm", &ftShm, 0, "Frag table; shared memory stats"},
		{"ft_size", &ft_size, 0, "Frag table; size"},
		{"ft_buckets", &ft_buckets, 0, "Frag table; extra buckets"},
		{"ft_frag", &ft_frag, 0, "Frag table; stored frags"},
		{"ft_ttl", &ft_ttl, 0, "Frag table; ttl milliS"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);

	if (lbShm != NULL) {
		slb = mapSharedDataOrDie(lbShm, O_RDONLY);
		magDataDyn_map(&magdlb, slb->mem);
	}

	fset = flowSetCreate(loadbalancerLock);
	if (promiscuous_ping == NULL)
		flowSetPromiscuousPing(fset, 1);

	// Create and re-map the stats struct
	sft = calloc(1, sizeof(*sft));
	createSharedDataOrDie(ftShm, sft, sizeof(*sft));
	free(sft);
	sft = mapSharedDataOrDie(ftShm, O_RDWR);

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

static void loadbalancerLock(void* user_ref)
{
	struct LoadBalancer* lb = user_ref;
	REFINC(lb->refCounter);
}
static void loadbalancerRelease(struct LoadBalancer* lb)
{
	if (lb == NULL)
		return;

	int refCounter = REFDEC(lb->refCounter);
	assert(refCounter >= 0);
	if (refCounter == 0) {
		Dx(printf("Delete load-balancer; %s\n", lb->target));
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

static struct LoadBalancer* loadbalancerFindOrCreate(char const* target)
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
			Dx(printf("Found LB; %s\n", target));
			return lb;
		}
	}

	// Not found, create a new LB
	Dx(printf("Creating LB; %s\n", target));
	int fd;
	struct SharedData* st = mapSharedDataRead(target, &fd);
	if (st == NULL) {
		UNLOCK(lblistLock);
		Dx(printf("Map shm failed; %s\n", target));
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

#define MAX_CMD_LINE 1024

// Incoming command structure
struct Cmd {
	char const* action;
	char const* name;
	char const* target;
	int priority;
	char const** protocols;
	char const* dports;
	char const* sports;
	char const** dsts;
	char const** srcs;
	unsigned short udpencap;
};

static int readCmd(FILE* in, struct Cmd* cmd)
{
	char buf[MAX_CMD_LINE];
	for (;;) {
		if (fgets(buf, sizeof(buf), in) == NULL) {
			Dx(printf("readCmd; unexpected eof\n"));
			return -1;
		}
		buf[strcspn(buf, "\n")] = 0; /* trim newline */
		Dx(printf("Cmd [%s]\n", buf));
		if (strncmp(buf, "eoc:", 4) == 0)
			break;			/* end-of-command */
		char* arg = strchr(buf, ':');
		if (arg == NULL)
			return -1;
		*arg++ = 0;
		if (strcmp(buf, "action") == 0) {
			if (cmd->action == NULL)
				cmd->action = strdup(arg);
		} else if (strcmp(buf, "name") == 0) {
			if (cmd->name == NULL)
				cmd->name = strdup(arg);
		} else if (strcmp(buf, "target") == 0) {
			if (cmd->target == NULL)
				cmd->target = strdup(arg);
		} else if (strcmp(buf, "priority") == 0) {
			cmd->priority = atoi(arg);
		} else if (strcmp(buf, "protocols") == 0) {
			if (cmd->protocols == NULL)
				cmd->protocols = mkargv(arg, ", ");
		} else if (strcmp(buf, "dports") == 0) {
			if (cmd->dports == NULL)
				cmd->dports = strdup(arg);
		} else if (strcmp(buf, "sports") == 0) {
			if (cmd->sports == NULL)
				cmd->sports = strdup(arg);
		} else if (strcmp(buf, "dsts") == 0) {
			if (cmd->dsts == NULL)
				cmd->dsts = mkargv(arg, ", ");
		} else if (strcmp(buf, "srcs") == 0) {
			if (cmd->srcs == NULL)
				cmd->srcs = mkargv(arg, ", ");
		} else if (strcmp(buf, "udpencap") == 0) {
			cmd->udpencap = atoi(arg);
		} else {
			// Unrecognized command ignored
		}
	}
	return 0;
}

static void writeReply(int fd, char const* msg)
{
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
	int sd = socket(AF_LOCAL, SOCK_STREAM, 0);
	struct sockaddr_un sa;
	sa.sun_family = AF_UNIX;
	sa.sun_path[0] = 0;
	strcpy(sa.sun_path+1, "nfqlb");
	socklen_t len =
		offsetof(struct sockaddr_un, sun_path) + strlen(sa.sun_path+1) + 1;
	if (bind(sd, (struct sockaddr*)&sa, len) != 0)
		die("bind\n");
	if (listen(sd, 128) != 0)
		die("listen\n");
	FILE* in = NULL;
	FILE* out = NULL;
	for (;;) {
		// Cleanup
		if (in != NULL)
			fclose(in);
		if (out != NULL)
			fclose(out);
		in = out = NULL;

		int cd = accept(sd, NULL, NULL);
		Dx(printf("Accepted incoming connection. cd=%d\n", cd));
		if (cd < 0)
			continue;
		in = fdopen(cd, "r");
		if (in == NULL) {
			close(cd);
			continue;
		}
		struct Cmd cmd;
		memset(&cmd, 0, sizeof(cmd));
		if (readCmd(in, &cmd) != 0)
			continue;
		if (cmd.action == NULL) {
			writeReply(cd, "FAIL: no action");
			continue;
		}

		if (strcmp(cmd.action, "set") == 0) {
			struct LoadBalancer* lb = loadbalancerFindOrCreate(cmd.target);
			if (lb == NULL) {
				writeReply(cd, "FAIL: Couldn't create load-balancer");
			} else {
				if (cmd.udpencap != 0) {
					/*
					  If a UDP encapsulated SCTP port is defined the
					  protocols must be "sctp" only.
					*/
					if (cmd.protocols == NULL || cmd.protocols[1] != NULL ||
						strcasecmp(cmd.protocols[0], "sctp") != 0) {
						writeReply(cd, "FAIL: only sctp for updencap");
						continue;
					}
					/*
					  The defined set will never match since the
					  incoming encapsulated sctp will actually be a
					  udp packet with dport=udpencap. So we must
					  insert a flow for the encapsulating udp also.
					*/
					char udpname[MAX_CMD_LINE];
					udpname[0] = '#';
					strncpy(udpname+1, cmd.name, MAX_CMD_LINE-2);
					const char* udpproto[] = {"udp", NULL};
					char udpdport[16];
					sprintf(udpdport, "%u", cmd.udpencap);
					if (flowDefine(
							fset, udpname, cmd.priority, NULL, udpproto, udpdport,
							NULL, cmd.dsts, cmd.srcs, cmd.udpencap) != 0) {
						writeReply(cd, "FAIL: UDP flow for updencap");
						continue;
					}
				}
				if (flowDefine(
						fset, cmd.name, cmd.priority, lb, cmd.protocols,
						cmd.dports, cmd.sports, cmd.dsts, cmd.srcs,
						cmd.udpencap) == 0) {
					writeReply(cd, "OK");
				} else {
					writeReply(cd, "FAIL: define flow");
				}
			}
		} else if (strcmp(cmd.action, "delete") == 0) {
			if (cmd.name != NULL) {
				unsigned short udpencap = 0;
				loadbalancerRelease(flowDelete(fset, cmd.name, &udpencap));
				if (udpencap > 0) {
					/*
					  This is a sctp flow with encapsulater udp. We
					  have a associated udp flow that must also be
					  deleted. The udp flow has not reserved the loadbalancer.
					 */
					char udpname[MAX_CMD_LINE];
					udpname[0] = '#';
					strncpy(udpname+1, cmd.name, MAX_CMD_LINE-2);
					flowDelete(fset, udpname, NULL);
				}
				writeReply(cd, "OK");
			} else {
				writeReply(cd, "FAIL: no name");
			}
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
	