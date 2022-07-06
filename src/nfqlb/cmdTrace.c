/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2022 Nordix Foundation
*/
#define _GNU_SOURCE
#include "nfqlb.h"
#include <cmd.h>
#include <log.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

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
		{"flows", TRACE_FLOWS},
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
	char const* trace_address = DEFAULT_TRACE_ADDRESS;
	struct Option options[] = {
		{"help", NULL, 0,
		 "trace [options]\n"
		 "  Initiate trace and direct output to stdout"},
		{"mask", &traceMaskOpt, 0, "Trace mask (32-bit). Most for debug"},
		{"selection", &traceSelectionOpt, 0,
		 "Trace selection. A comma separated list of;\n"
		 "     log,packet,frag,flow-conf,sctp,target,flows"},
		{"trace_address", &trace_address, 0, "Trace server address"},
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

	struct sockaddr_storage sa;
	socklen_t len;
	if (parseAddress(trace_address, &sa, &len) != 0)
		die("Failed to parse address [%s]", trace_address);
	
	int sd = socket(sa.ss_family, SOCK_STREAM, 0);
	if (sd < 0)
		die("Trace client socket: %s\n", strerror(errno));
	if (connect(sd, (struct sockaddr*)&sa, len) != 0)
		die("Trace client connect to %s: %s\n", trace_address, strerror(errno));

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

static int cmdTraceFlowSet(int argc, char **argv)
{
	char const* name = NULL;
	char const* protocols = NULL;
	char const* dsts = NULL;
	char const* srcs = NULL;
	char const* dports = NULL;
	char const* sports = NULL;
	char const* match = NULL;
	struct Option options[] = {
		{"help", NULL, 0,
		 "trace-flow-set [options]\n"
		 "  Set a flow. An un-defined value means match-all.\n"
		 "  Use comma separated lists for multiple items (no spaces)"},
		{"name", &name, REQUIRED, "Name of the flow"},
		{"protocols", &protocols, 0, "Protocols. tcp, udp, sctp"},
		{"dsts", &dsts, 0, "Destination CIDRs"},
		{"srcs", &srcs, 0, "Source CIDRs"},
		{"dports", &dports, 0, "Destination port ranges"},
		{"sports", &sports, 0, "Source port ranges"},
		{"match", &match, 0, "Bit-match statements"},		
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);

	// Sanity checks
	if (protocols != NULL && strlen(protocols) > MAX_ARG_LEN)
		die("protocols too long\n");
	if (dsts != NULL && strlen(dsts) > MAX_ARG_LEN)
		die("dsts too long\n");
	if (srcs != NULL && strlen(srcs) > MAX_ARG_LEN)
		die("srcs too long\n");
	if (dports != NULL && strlen(dports) > MAX_ARG_LEN)
		die("dports too long\n");
	if (sports != NULL && strlen(sports) > MAX_ARG_LEN)
		die("sports too long\n");
	if (match != NULL && strlen(match) > MAX_ARG_LEN)
		die("match too long\n");
	if ((dports != NULL || sports != NULL) && protocols == NULL)
		die("ports specified without protocols\n");
	
	int cd = connectToLb();

	FILE* out = stream(cd, "w");
	fprintf(out, "action:trace-set\n");
	fprintf(out, "name:%s\n", name);
	if (protocols != NULL)
		fprintf(out, "protocols:%s\n", protocols);
	if (dsts != NULL)
		fprintf(out, "dsts:%s\n", dsts);
	if (srcs != NULL)
		fprintf(out, "srcs:%s\n", srcs);
	if (dports != NULL)
		fprintf(out, "dports:%s\n", dports);
	if (sports != NULL)
		fprintf(out, "sports:%s\n", sports);
	if (match != NULL)
		fprintf(out, "match:%s\n", match);
	fprintf(out, "eoc:\n");
	fflush(out);

	char buf[64];
	if (read(cd, buf, sizeof(buf)) > 0) {
		printf("%s\n", buf);
		if (strncmp(buf, "OK", 2) == 0)
			return 0;
	}

	return -1;
}
static int cmdTraceFlowDelete(int argc, char **argv)
{
	char const* name = NULL;
	struct Option options[] = {
		{"help", NULL, 0,
		 "trace-flow-delete\n"
		 "  Delete a flow"},
		{"name", &name, REQUIRED, "Name of the flow"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);
	int cd = connectToLb();
	FILE* out = stream(cd, "w");
	fprintf(out, "action:trace-delete\n");
	fprintf(out, "name:%s\n", name);
	fprintf(out, "eoc:\n");
	fflush(out);

	char buf[64];
	if (read(cd, buf, sizeof(buf)) > 0) {
		printf("%s\n", buf);
		if (strncmp(buf, "OK", 2) == 0)
			return 0;
	}
	return -1;
}

static int cmdTraceFlowList(int argc, char **argv)
{
	char const* name = NULL;
	struct Option options[] = {
		{"help", NULL, 0,
		 "trace-flow-list\n"
		 "  List flows"},
		{"name", &name, 0, "Name of a flow"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);
	int cd = connectToLb();
	FILE* out = stream(cd, "w");
	fprintf(out, "action:trace-list\n");
	if (name != NULL)
		fprintf(out, "name:%s\n", name);
	fprintf(out, "eoc:\n");
	fflush(out);
	char buf[1024];
	int n;
	while ((n = read(cd, buf, sizeof(buf))) > 0) {
		write(1, buf, n);
	}
	return 0;
}

static int cmdTraceFlowListNames(int argc, char **argv)
{
	struct Option options[] = {
		{"help", NULL, 0,
		 "trace-flow-list-names\n"
		 "  List flow names"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);
	int cd = connectToLb();
	if (cd < 0)
		die("Connect failed. %s\n", strerror(errno));
	FILE* out = stream(cd, "w");
	fprintf(out, "action:trace-list-names\n");
	fprintf(out, "eoc:\n");
	fflush(out);
	char buf[1024];
	int n;
	while ((n = read(cd, buf, sizeof(buf))) > 0) {
		write(1, buf, n);
	}
	return 0;
}

__attribute__ ((__constructor__)) static void addCommands(void) {
	addCmd("trace", cmdTrace);
	addCmd("loglevel", cmdLoglevel);
	addCmd("trace-flow-set", cmdTraceFlowSet);
	addCmd("trace-flow-delete", cmdTraceFlowDelete);
	addCmd("trace-flow-list", cmdTraceFlowList);
	addCmd("trace-flow-list-names", cmdTraceFlowListNames);
}

