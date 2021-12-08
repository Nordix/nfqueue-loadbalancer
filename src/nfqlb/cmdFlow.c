/*
   SPDX-License-Identifier: Apache-2.0
   Copyright (c) 2021 Nordix Foundation
*/

#include <cmd.h>
#include <die.h>
#define _GNU_SOURCE				/* (for strcasestr) */
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define MAX_ARG_LEN 1000

static int connectToLb(void)
{
	int sd = socket(AF_LOCAL, SOCK_STREAM, 0);
	struct sockaddr_un sa;
	sa.sun_family = AF_UNIX;
	sa.sun_path[0] = 0;
	strcpy(sa.sun_path+1, "nfqlb");
	socklen_t len =
		offsetof(struct sockaddr_un, sun_path) + strlen(sa.sun_path+1) + 1;
	if (connect(sd, (struct sockaddr*)&sa, len) != 0) {
		close(sd);
		return -1;
	}
	return sd;
}

static FILE* stream(int sd, char const* perm)
{
	FILE* f = fdopen(sd, perm);
	if (f == NULL)
		die("fdopen: %s\n", strerror(errno));
	return f;
}

static int cmdFlowSet(int argc, char **argv)
{
	char const* name = NULL;
	char const* target = NULL;
	char const* prio = NULL;
	char const* protocols = NULL;
	char const* dsts = NULL;
	char const* srcs = NULL;
	char const* dports = NULL;
	char const* sports = NULL;
	char const* udpencap = NULL;
	struct Option options[] = {
		{"help", NULL, 0,
		 "flow-set [options]\n"
		 "  Set a flow. An un-defined value means match-all.\n"
		 "  Use comma separated lists for multiple items (no spaces)"},
		{"name", &name, REQUIRED, "Name of the flow"},
		{"target", &target, REQUIRED, "Name of SHM for the load-balancer"},
		{"prio", &prio, 0, "Priority. 0 has lowest precedence (default)"},
		{"protocols", &protocols, 0, "Protocols. tcp, udp, sctp"},
		{"dsts", &dsts, 0, "Destination CIDRs"},
		{"srcs", &srcs, 0, "Source CIDRs"},
		{"dports", &dports, 0, "Destination port ranges"},
		{"sports", &sports, 0, "Source port ranges"},
		{"udpencap", &udpencap, 0, "UDP encapsulation port for SCTP"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);

	// Sanity checks
	if (target != NULL && strlen(target) > 255)
		die("target too long\n");
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
	if (udpencap > 0 && protocols == 0)
		die("udpencap specified without protocols\n");
	if (udpencap > 0 && (strcasestr(protocols, "sctp") == NULL))
		die("udpencap specified without sctp\n");
	if ((dports != NULL || sports != NULL) && protocols == NULL)
		die("ports specified without protocols\n");
	
	int cd = connectToLb();
	if (cd < 0)
		die("Connect failed. %s\n", strerror(errno));
	FILE* out = stream(cd, "w");
	fprintf(out, "action:set\n");
	fprintf(out, "name:%s\n", name);
	fprintf(out, "target:%s\n", target);
	if (prio != NULL)
		fprintf(out, "priority:%s\n", prio);
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
	if (udpencap != NULL)
		fprintf(out, "udpencap:%s\n", udpencap);
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

static int cmdFlowDelete(int argc, char **argv)
{
	char const* name = NULL;
	struct Option options[] = {
		{"help", NULL, 0,
		 "flow-delete\n"
		 "  Delete a flow"},
		{"name", &name, REQUIRED, "Name of the flow"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);
	int cd = connectToLb();
	if (cd < 0)
		die("Connect failed. %s\n", strerror(errno));
	FILE* out = stream(cd, "w");
	fprintf(out, "action:delete\n");
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

static int cmdFlowList(int argc, char **argv)
{
	char const* name = NULL;
	struct Option options[] = {
		{"help", NULL, 0,
		 "flow-list\n"
		 "  List flows"},
		{"name", &name, 0, "Name of a flow"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);
	int cd = connectToLb();
	if (cd < 0)
		die("Connect failed. %s\n", strerror(errno));
	FILE* out = stream(cd, "w");
	fprintf(out, "action:list\n");
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

static int cmdFlowListNames(int argc, char **argv)
{
	struct Option options[] = {
		{"help", NULL, 0,
		 "flow-list-names\n"
		 "  List flow names"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);
	int cd = connectToLb();
	if (cd < 0)
		die("Connect failed. %s\n", strerror(errno));
	FILE* out = stream(cd, "w");
	fprintf(out, "action:list-names\n");
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
	addCmd("flow-set", cmdFlowSet);
	addCmd("flow-delete", cmdFlowDelete);
	addCmd("flow-list", cmdFlowList);
	addCmd("flow-list-names", cmdFlowListNames);
}
