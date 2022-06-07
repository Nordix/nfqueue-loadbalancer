/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2022 Nordix Foundation
*/

#include "nfqlb.h"
#include <argv.h>
#include <log.h>
#include <string.h>
#include <stdlib.h>

int readFlowCmd(FILE* in, struct FlowCmd* cmd)
{
	char buf[MAX_CMD_LINE];
	memset(cmd, 0, sizeof(*cmd));
	for (;;) {
		if (fgets(buf, sizeof(buf), in) == NULL) {
			warning("readFlowCmd; unexpected eof\n");
			freeFlowCmd(cmd);
			return -1;
		}
		buf[strcspn(buf, "\n")] = 0; /* trim newline */
		trace(TRACE_FLOW_CONF, "FlowCmd [%s]\n", buf);
		if (strncmp(buf, "eoc:", 4) == 0)
			break;			/* end-of-command */
		char* arg = strchr(buf, ':');
		if (arg == NULL) {
			warning("readFlowCmd; invalid param [%s]\n", buf);
			freeFlowCmd(cmd);
			return -1;
		}
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
		} else if (strcmp(buf, "match") == 0) {
			if (cmd->match == NULL)
				cmd->match = mkargv(arg, ",");
		} else if (strcmp(buf, "udpencap") == 0) {
			cmd->udpencap = atoi(arg);
		} else {
			// Unrecognized command ignored
			warning("readCmd; Unrecognized command [%s]\n", buf);
			trace(TRACE_FLOW_CONF, "readCmd; Unrecognized command [%s]\n", buf);
		}
	}
	return 0;
}

void freeFlowCmd(struct FlowCmd* cmd)
{
	free((void*)cmd->action);
	free((void*)cmd->name);
	free((void*)cmd->target);
	free((void*)cmd->protocols);
	free((void*)cmd->dports);
	free((void*)cmd->sports);
	free((void*)cmd->dsts);
	free((void*)cmd->srcs);
	free((void*)cmd->match);
	memset(cmd, 0, sizeof(*cmd));
}

int connectToLb(void)
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
		die("Client socket: %s\n", strerror(errno));
	if (connect(sd, (struct sockaddr*)&sa, len) != 0)
		die("Connect failed. %s\n", strerror(errno));
	return sd;
}

FILE* stream(int sd, char const* perm)
{
	FILE* f = fdopen(sd, perm);
	if (f == NULL)
		die("fdopen: %s\n", strerror(errno));
	return f;
}
