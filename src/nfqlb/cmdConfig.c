/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

#include "nfqueue.h"
#include <cmd.h>
#include <shmem.h>
#include <stdlib.h>

static void maglevSetActive(
	struct MagData* m, unsigned v, int argc, char *argv[])
{
	while (argc-- > 0) {
		int i = atoi(*argv++);
		if (i >= 0 && i < m->N) m->active[i] = v;
	}
	populate(m);
}

static int cmdActivate(int argc, char **argv)
{
	char const* shm = defaultTargetShm;
	struct Option options[] = {
		{"help", NULL, 0,
		 "activate [options]\n"
		 "  Activate a target or lb"},
		{"shm", &shm, 0, "Shared memory"},
		{0, 0, 0, 0}
	};
	int nopt = parseOptionsOrDie(argc, argv, options);
	argc -= nopt;
	argv += nopt;
	struct SharedData* s;
	s = mapSharedDataOrDie(shm, sizeof(*s), O_RDWR);
	maglevSetActive(&s->magd, 1, argc, argv);
	return 0;
}

static int cmdDeactivate(int argc, char **argv)
{
	char const* shm = defaultTargetShm;
	struct Option options[] = {
		{"help", NULL, 0,
		 "deactivate [options]\n"
		 "  Deactivate a target or lb"},
		{"shm", &shm, 0, "Shared memory"},
		{0, 0, 0, 0}
	};
	int nopt = parseOptionsOrDie(argc, argv, options);
	argc -= nopt;
	argv += nopt;
	struct SharedData* s;
	s = mapSharedDataOrDie(shm, sizeof(*s), O_RDWR);
	maglevSetActive(&s->magd, 0, argc, argv);
	return 0;
}

__attribute__ ((__constructor__)) static void addCommands(void) {
	addCmd("activate", cmdActivate);
	addCmd("deactivate", cmdDeactivate);
}
