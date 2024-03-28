/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021-2022 Nordix Foundation
*/

#include "nfqueue.h"
#include <cmd.h>
#include <die.h>
#include <shmem.h>
#include <maglevdyn.h>
#include <iputils.h>
#include <conntrack.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void adrParse(char const* str, /*out*/ struct sockaddr_in6* adr);

static int cmdFwmark(int argc, char **argv)
{
	char const* shm = NULL;
	char const* src = NULL;
	char const* dst = NULL;
	char const* proto = NULL;
	char const* lb_hash_mode = "1";
	struct Option options[] = {
		{"help", NULL, 0,
		 "fwmark [--shm=] [--proto=tcp|udp|sctp] --src=addr:port --dst=addr:port\n"
		 "  Print hash and fwmark for the specified addresses for debug"
		},
		{"shm", &shm, 0, "Shared memory. Required for fwmark printout"},
		{"proto", &proto, 0, "Protocol, tcp|udp|sctp. NOT specified -> address-only hash"},
		{"src", &src, 1, "Source addr:port, e.g \"[1000::80]:80\"" },
		{"dst", &dst, 1, "Destination addr:port"},
		{"hash_mode", &lb_hash_mode, 0, "Load balance with a different hash mode. 0: Tuple-5, 1: SCTP Ports only. default=1"},
		{0, 0, 0, 0}
	};
	int nopt = parseOptionsOrDie(argc, argv, options);
	argc -= nopt;
	argv += nopt;

	struct ctKey key = {0};
	struct sockaddr_in6 adr;

	adrParse(src, &adr);
	key.src = adr.sin6_addr;
	key.ports.src = adr.sin6_port;

	adrParse(dst, &adr);
	key.dst = adr.sin6_addr;
	key.ports.dst = adr.sin6_port;

	unsigned hash_mode = atoi(lb_hash_mode);

	unsigned hash;
	if (proto != NULL) {
		key.ports.proto = parseProto(proto);
		if (key.ports.proto == 0)
			die("Failed to parse protocol [%s]\n", proto);
		hash = hashKey(&key, hash_mode);
	} else {
		hash = hashKeyAddresses(&key);
	}

	if (shm != NULL) {
		struct SharedData* s = NULL;
		s = mapSharedDataOrDie(shm, O_RDWR);
		struct MagDataDyn magd;
		magDataDyn_map(&magd, s->mem);
		unsigned index = hash % magd.M;
		int activeindex = magd.lookup[index];
		int fwmark = -1;
		if (activeindex >= 0)
			fwmark = magd.active[activeindex];
		printf(
			"{ \"hash\": %u, \"lookupindex\": %d, \"activeindex\": %d, \"fwmark\": %d }\n",
			hash, index, activeindex, fwmark);
	} else {
		printf("{ \"hash\": %u }\n", hash);
	}

	return 0;
}

static void adrParse(char const* str, /*out*/ struct sockaddr_in6* adr)
{
	if (strlen(str) > 60)
		die("Address too long [%s]\n", str);
	char buf[128];
	if (strchr(str, '[') != NULL) {
		snprintf(buf, sizeof(buf), "tcp:%s", str);
	} else {
		char const* portp = strrchr(str, ':');
		if (portp == NULL)
			die("No port in [%s]\n", str);
		snprintf(buf, sizeof(buf), "tcp:[::ffff:%s", str);
		// "tcp:[::ffff:127.1.1.1:80" -> "tcp:[::ffff:127.1.1.1]:80"
		char* cp = strrchr(buf, ':');
		*cp = ']';
		strcpy(cp+1, portp);
	}

	socklen_t len = sizeof(struct sockaddr_in6);
	if (parseAddress(buf, (struct sockaddr_storage*)adr, &len) != 0)
		die("Parse address failed [%s]\n", str);
}

__attribute__ ((__constructor__)) static void addCommands(void) {
	addCmd("fwmark", cmdFwmark);
}
