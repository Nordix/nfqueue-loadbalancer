/*
   SPDX-License-Identifier: Apache-2.0
   Copyright (c) 2021 Nordix Foundation
*/

#include "cmd.h"
#include <die.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#ifndef MAX_COMMANDS
#define MAX_COMMANDS 20
#endif

struct Cmd {
	char const* name;
	int (*fn)(int argc, char* argv[]);
};
static struct Cmd cmd[MAX_COMMANDS + 1] = {{NULL, NULL}};
void addCmd(char const* name, int (*fn)(int argc, char* argv[]))
{
	struct Cmd* c = cmd;
	while (c->name != NULL) c++;
	if (c - cmd < MAX_COMMANDS) {
		c->name = name;
		c->fn = fn;
	}
}

int handleCmd(int argc, char *argv[])
{
	if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
		printf("Usage: %s <command> [opt...]\nCommands:\n", argv[0]);
		for (struct Cmd const* c = cmd; c->name != NULL; c++) {
			printf("  %s\n", c->name);
		}
		return EXIT_SUCCESS;
	}

	argc--;
	argv++;
	for (struct Cmd* c = cmd; c->fn != NULL; c++) {
		if (strcmp(*argv, c->name) == 0)
			return c->fn(argc, argv);
	}

	fprintf(stderr, "Unknown command [%s]\n", *argv);
	return EXIT_FAILURE;
}

static int verifyRequiredOptions(
	struct option const* long_options, unsigned required, unsigned got)
{
	got = got & required;
	if (required == got) return 0;
	unsigned i, m;
	for (i = 0; i < 32; i++) {
		m = (1 << i);
		if ((required & m) != (got & m)) {
			char const* opt = "(unknown)";
			struct option const* o;
			for (o = long_options; o->name != NULL; o++) {
				if (o->val == i) {
					opt = o->name;
					break;
				}
			}
			fprintf(stderr, "Missing option [--%s]\n", opt);
		}
	}
	return -1;
}

static void printUsage(struct Option const* options)
{
	struct Option const* o;
	for (o = options; o->name != NULL; o++) {
		if (strcmp(o->name, "help") == 0) {
			puts(o->help);
			break;
		}
	}
	for (o = options; o->name != NULL; o++) {
		if (strcmp(o->name, "help") == 0)
			continue;
		printf(
			"  --%s= %s %s\n",
			o->name, o->help, o->flags & REQUIRED ? "(required)":"");
	}
}

int parseOptions(int argc, char* argv[], struct Option const* options)
{
	unsigned required = 0;
	int i, len = 0;
	struct Option const* o;
	for (o = options; o->name != NULL; o++)
		len++;
	if (len >= 32)
		die("Too many options %d (max 31)\n", len);
	struct option long_options[len+1];
	memset(long_options, 0, sizeof(long_options));
	for (i = 0; i < len; i++) {
		o = options + i;
		struct option* lo = long_options + i;
		lo->name = o->name;
		lo->has_arg = no_argument;
		if (o->arg != NULL && strcmp(*o->arg, "no") != 0)
			lo->has_arg = required_argument;
		lo->val = i;
		if (o->flags & REQUIRED)
			required |= (1 << i);
	}

	int option_index = 0;
	unsigned got = 0;
	i = getopt_long_only(argc, argv, "", long_options, &option_index);
	while (i >= 0) {
		if (i >= 32)
			return -1;
		got |= (1 << i);
		o = options + i;
		if (strcmp(o->name, "help") == 0) {
			printUsage(options);
			return 0;
		}
		if (o->arg != NULL)
			*(o->arg) = optarg;
		i = getopt_long_only(argc, argv, "", long_options, &option_index);
	}
	if (verifyRequiredOptions(long_options, required, got) != 0)
		return -1;
	return optind;
}

int parseOptionsOrDie(int argc, char* argv[], struct Option const* options)
{
	int nopt = parseOptions(argc, argv, options);
	if (nopt > 0)
		return nopt;
	exit(nopt == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}

