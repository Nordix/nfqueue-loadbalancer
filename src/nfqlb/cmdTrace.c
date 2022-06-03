/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2022 Nordix Foundation
*/
#define _GNU_SOURCE
#include "nfqlb.h"
#include <cmd.h>
#include <die.h>
#include <log.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stddef.h>
#include <stdlib.h>

static unsigned str2mask(char const* name)
{
	static const struct TraceEvent {
		char const* name;
		unsigned mask;
	} traceEvent[] = {
		{"log", TRACE_LOG},
		{"packet", TRACE_PACKET},
		{"frag", TRACE_FRAG},
		{"flow-conf", TRACE_FLOW_CONF},
		{"sctp", TRACE_SCTP},
		{"target", TRACE_TARGET},
		{NULL, 0}	
	};

	struct TraceEvent const* e;
	for (e = traceEvent; e->name != NULL; e++) {
		if (strcmp(name, e->name) == 0)
			return e->mask;
	}
	return 0;
}


static int cmdTrace(int argc, char **argv)
{
	char const* traceMaskOpt = NULL;
	char const* traceSelectionOpt = NULL;
	struct Option options[] = {
		{"help", NULL, 0,
		 "trace [options]\n"
		 "  Initiate trace and direct output to stdout"},
		{"mask", &traceMaskOpt, 0, "Trace mask (32-bit). Most for debug"},
		{"selection", &traceSelectionOpt, 0,
		 "Trace selection. A comma separated list of;\n"
		 "     log,packet,frag,flow-conf,sctp,target"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);
	if (traceMaskOpt == NULL && traceSelectionOpt == NULL)
		die("Specify any of --mask or --selection");
	if (traceMaskOpt != NULL && traceSelectionOpt != NULL)
		die("Specify any ONE of --mask or --selection");

	uint32_t mask = 0;
	if (traceSelectionOpt != NULL) {
		char* sel = strdupa(traceSelectionOpt);
		char* t;
		for (t = strtok(sel, ","); t != NULL; t = strtok(NULL, ",")) {
			mask |= str2mask(t);
		}
	} else {
		mask = strtol(traceMaskOpt, NULL, 0);
	}
	printf("Mask=0x%08x\n", mask);

	logConfigShm(TRACE_SHM);
	LOG_SET_TRACE_MASK(mask);

	int sd = socket(AF_LOCAL, SOCK_STREAM, 0);
	struct sockaddr_un sa = {0};
	sa.sun_family = AF_UNIX;
	sa.sun_path[0] = 0;
	strcpy(sa.sun_path+1, TRACE_UNIX_SOCK);
	socklen_t len =
		offsetof(struct sockaddr_un, sun_path) + strlen(sa.sun_path+1) + 1;
	if (connect(sd, (struct sockaddr*)&sa, len) != 0)
		die("FAILED: connect\n");

	char buffer[4*1024];
	int rc = read(sd, buffer, sizeof(buffer));
	while (rc > 0) {
		if (write(1, buffer, rc) != rc)
			die("FAILED: write\n");
		rc = read(sd, buffer, sizeof(buffer));
	}

	return 0;
}

static int cmdLoglevel(int argc, char **argv)
{
	struct Option options[] = {
		{"help", NULL, 0,
		 "loglevel [level]\n"
		 "  Set log level in shared mem"},
		{0, 0, 0, 0}
	};
	int nopt = parseOptionsOrDie(argc, argv, options);
	argc -= nopt; argv += nopt;

	logConfigShm(TRACE_SHM);
	if (argc > 0)
		LOG_SET_LEVEL(atoi(argv[0]));

	printf("loglevel=%d\n", LOG_LEVEL);
	return 0;
}

__attribute__ ((__constructor__)) static void addCommands(void) {
	addCmd("trace", cmdTrace);
	addCmd("loglevel", cmdLoglevel);
}

