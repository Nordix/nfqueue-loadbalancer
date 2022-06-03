/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2022 Nordix Foundation
*/
#if 0
#include "trace.h"

#include <nfqueue.h>
#include <cmd.h>
#include <die.h>
#include <flow.h>
#include <argv.h>
#include <log.h>

#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>

static struct FlowSet* fset;

static void* traceFlowThread(void* a)
{
	int sd = socket(AF_LOCAL, SOCK_STREAM, 0);
	struct sockaddr_un sa;
	sa.sun_family = AF_UNIX;
	sa.sun_path[0] = 0;
	strcpy(sa.sun_path+1, TRACE_UNIX_SOCK);
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
		debug("traceFlowThread: Accepted incoming connection. cd=%d\n", cd);
		if (cd < 0) {
			warning("traceFlowThread: accept returns %d\n", cd);
			continue;
		}
		in = fdopen(cd, "r");
		if (in == NULL) {
			warning("flowThreadh: fdopen failed\n");
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
			char const* err;
			err = flowDefine(
				fset, cmd.name, cmd.priority, NULL, cmd.protocols,
				cmd.dports, cmd.sports, cmd.dsts, cmd.srcs,
				cmd.match, cmd.udpencap);
			if (err != NULL) {
				writeReply(cd, err);
				continue;
			}
		} else if (strcmp(cmd.action, "delete") == 0) {
			if (cmd.name != NULL) {
				flowDelete(fset, cmd.name, NULL);
				writeReply(cd, "OK");
			} else {
				writeReply(cd, "FAIL: no name");
			}
		} else if (strcmp(cmd.action, "list") == 0) {
			out = fdopen(dup(cd), "w");
			if (out == NULL)
				writeReply(cd, "FAIL: fdopen");
			else {
				flowSetPrint(out, fset, cmd.name, NULL);
				fflush(out);
			}
		} else if (strcmp(cmd.action, "list-names") == 0) {
			out = fdopen(dup(cd), "w");
			if (out == NULL)
				writeReply(cd, "FAIL: fdopen");
			else {
				flowSetPrintNames(out, fset);
				fflush(out);
			}
		} else {
			writeReply(cd, "FAIL: action unknown");
		}
	}
	return NULL;
}
#endif
