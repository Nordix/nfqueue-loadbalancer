#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

#include <flow.h>
#include <maglevdyn.h>
#include <shmem.h>
#include <nfqueue.h>
#include "nfqlb.h"

extern struct FlowSet* fset;
extern struct LoadBalancer* lblist;
extern void loadbalancerLock(void* user_ref);
extern void loadbalancerRelease(struct LoadBalancer* lb);
extern struct LoadBalancer* loadbalancerFindOrCreate(char const* target);
extern void cmd_set(struct FlowCmd* cmd, int cd);
extern void cmd_delete(struct FlowCmd* cmd, int cd);

// COPIED FROM cmdFlowLb.c. KEEP IN SYNC!
struct LoadBalancer {
	struct LoadBalancer* next;
	int refCounter;
	char* target;
	int fd;
	struct SharedData* st;
	struct MagDataDyn magd;
};

static void initShm(char const* name, int ownFw, unsigned m, unsigned n);
static int countLb(void);
static int readResult(int fd);

int main(int argc, char* argv[])
{
	struct LoadBalancer* lb;
	struct FlowCmd cmd;
	int pipe[2];
	char* protocols[2];

	// Init
	shm_unlink("lb100");
	shm_unlink("lb200");
	assert(pipe2(pipe, O_NONBLOCK) == 0);
	fset = flowSetCreate(loadbalancerLock);

	// LB handling
	assert(lblist == NULL);
	assert(loadbalancerFindOrCreate("lb100") == NULL);
	initShm("lb100", 0, 17, 2);
	initShm("lb200", 0, 17, 2);
	lb = loadbalancerFindOrCreate("lb100");
	assert(lb != NULL);
	assert(countLb() == 1);
	loadbalancerRelease(lb);
	assert(countLb() == 0);

	lb = loadbalancerFindOrCreate("lb100");
	assert(lb != NULL);
	assert(loadbalancerFindOrCreate("lb100") == lb);
	assert(countLb() == 1);
	loadbalancerRelease(lb);
	assert(countLb() == 1);
	loadbalancerRelease(lb);
	assert(countLb() == 0);

	// Flows
	memset(&cmd, 0, sizeof(cmd));
	cmd.name = "lb100";
	cmd.target = "lb100";
	cmd_set(&cmd, pipe[1]);
	assert(countLb() == 1);
	assert(lblist->refCounter == 1);
	assert(readResult(pipe[0]) == 0);
	assert(flowSetSize(fset) == 1);

	// Re-define must not increment the refCounter (issue #9)
	cmd_set(&cmd, pipe[1]);
	assert(countLb() == 1);
	assert(lblist->refCounter == 1);
	assert(readResult(pipe[0]) == 0);

	cmd.name = "lb100-2";
	cmd_set(&cmd, pipe[1]);
	assert(readResult(pipe[0]) == 0);
	assert(countLb() == 1);
	assert(lblist->refCounter == 2);

	cmd_delete(&cmd, pipe[1]);
	assert(readResult(pipe[0]) == 0);
	assert(countLb() == 1);
	assert(lblist->refCounter == 1);

	cmd.name = "lb100";
	cmd_delete(&cmd, pipe[1]);
	assert(readResult(pipe[0]) == 0);
	assert(countLb() == 0);
	assert(lblist == NULL);

	// Flows with udpencap
	memset(&cmd, 0, sizeof(cmd));
	cmd.name = "lb100";
	cmd.target = "lb100";
	protocols[0] = "sctp";
	protocols[1] = NULL;
	cmd.protocols = (const char**)protocols;
	cmd.dports = "2000";
	cmd.udpencap = 9000;
	cmd_set(&cmd, pipe[1]);
	assert(countLb() == 1);
	assert(lblist->refCounter == 1);
	assert(readResult(pipe[0]) == 0);
	assert(flowSetSize(fset) == 2);

	cmd_delete(&cmd, pipe[1]);
	assert(readResult(pipe[0]) == 0);
	assert(countLb() == 0);
	assert(lblist == NULL);
	assert(flowSetSize(fset) == 0);

	// Re-define a udpencap and delete
	cmd_set(&cmd, pipe[1]);
	assert(countLb() == 1);
	assert(lblist->refCounter == 1);
	assert(readResult(pipe[0]) == 0);
	assert(flowSetSize(fset) == 2);

	cmd_set(&cmd, pipe[1]);
	assert(countLb() == 1);
	assert(lblist->refCounter == 1);
	assert(readResult(pipe[0]) == 0);
	assert(flowSetSize(fset) == 2);
	
	cmd_delete(&cmd, pipe[1]);
	assert(readResult(pipe[0]) == 0);
	assert(countLb() == 0);
	assert(lblist == NULL);
	assert(flowSetSize(fset) == 0);

	// Re-define the upd-port should fail
	cmd_set(&cmd, pipe[1]);
	assert(countLb() == 1);
	assert(lblist->refCounter == 1);
	assert(readResult(pipe[0]) == 0);
	assert(flowSetSize(fset) == 2);

	cmd.udpencap = 8000;
	cmd_set(&cmd, pipe[1]);
	assert(readResult(pipe[0]) != 0);

	cmd_delete(&cmd, pipe[1]);
	assert(readResult(pipe[0]) == 0);
	assert(countLb() == 0);
	assert(lblist == NULL);
	assert(flowSetSize(fset) == 0);

	// Target re-define
	memset(&cmd, 0, sizeof(cmd));
	cmd.name = "lb100";
	cmd.target = "lb100";
	cmd_set(&cmd, pipe[1]);
	assert(countLb() == 1);
	assert(lblist->refCounter == 1);
	assert(readResult(pipe[0]) == 0);
	assert(flowSetSize(fset) == 1);

	cmd.target = "WRONG";
	cmd_set(&cmd, pipe[1]);
	assert(readResult(pipe[0]) != 0);
	assert(lblist->refCounter == 1);
	assert(flowSetSize(fset) == 1);
	assert(countLb() == 1);

	cmd.target = "lb200";
	cmd_set(&cmd, pipe[1]);
	assert(countLb() == 1);
	assert(lblist->refCounter == 1);
	assert(readResult(pipe[0]) == 0);
	assert(flowSetSize(fset) == 1);

	cmd_delete(&cmd, pipe[1]);
	assert(readResult(pipe[0]) == 0);
	assert(countLb() == 0);
	assert(lblist == NULL);
	assert(flowSetSize(fset) == 0);

	// Clean-up
	assert(shm_unlink("lb100") == 0);
	assert(shm_unlink("lb200") == 0);
	printf("=== cmdFlowLb-test; OK\n");
	return 0;
}

static int readResult(int fd)
{
	char buff[64];
	int rc = read(fd, buff, sizeof(buff));
	//printf("rc=%d, response [%s]\n", rc, buff);
	if (rc < 3) return -1; 
	if (rc > 3) printf("response [%s], rc=%d\n", buff, rc);
	if (strncmp(buff, "OK", 2) != 0)
		return -1;
	return 0;
}

static int countLb(void)
{
	int cnt = 0;
	struct LoadBalancer* lb;
	for (lb = lblist; lb != NULL; lb = lb->next) cnt++;
	return cnt;
}

static void initShm(
	char const* name, int ownFw, unsigned m, unsigned n)
{
	unsigned len = magDataDyn_len(m, n);
	struct SharedData* s = malloc(sizeof(struct SharedData) + len);
	s->ownFwmark = ownFw;
	createSharedDataOrDie(name, s, sizeof(struct SharedData) + len);
	free(s);
	s = mapSharedDataOrDie(name, O_RDWR, NULL);
	magDataDyn_init(m, n, s->mem, len);
}

void freeFlowCmd(struct FlowCmd* cmd){}
int readFlowCmd(FILE* in, struct FlowCmd* cmd) { return 0; }
