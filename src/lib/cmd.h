#pragma once
/*
   SPDX-License-Identifier: Apache-2.0
   Copyright (c) 2021 Nordix Foundation
*/

void addCmd(char const* name, int (*fn)(int argc, char* argv[]));
int handleCmd(int argc, char *argv[]);

/*
  Example;
	char const* shuffleStr = "no";   // "no" means no-argument-option
	char const* fileStr = "";        // Must not be NULL
	char const* repeatStr = "1";
	struct Option options[] = {
		{"help", NULL, 0,
		 "parse\n"
		 "  Read pcap file and parse fragments"},
		{"file", &fileStr, REQUIRED, "Pcap file"},
		{"shuffle", &shuffleStr, 0, "Shuffle the packets"},
		{"repeat", &repeatStr, 0, "Repeat n times"},
		{0, 0, 0, 0}
	};
	int nopt = parseOptionsOrDie(argc, argv, options);
	argc -= nopt;
	argv += nopt;
 */

struct Option {
	char const* const name;
	char const** arg;
#define REQUIRED 1
	unsigned flags;
	char const* const help;
};
// Returns number of handled items, < 0 on error, 0 on help
int parseOptions(int argc, char* argv[], struct Option const* options);
int parseOptionsOrDie(int argc, char* argv[], struct Option const* options);

