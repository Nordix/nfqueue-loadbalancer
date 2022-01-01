/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021-2022 Nordix Foundation
*/

#include "nfqueue.h"
#include <cmd.h>
#include <die.h>
#include <shmem.h>
#include <maglevdyn.h>

#include <stdlib.h>


static int cmdActivate(int argc, char **argv)
{
	char const* shm = defaultTargetShm;
	char const* index = NULL;
	struct Option options[] = {
		{"help", NULL, 0,
		 "activate [--shm=] <fwmarks...>\n"
		 "activate [--shm=] --index=# <fwmark>\n"
		 "  Activate targets."},
		{"shm", &shm, 0, "Shared memory"},
		{"index", &index, 0, "Index in the active table (<= N)"},
		{0, 0, 0, 0}
	};
	int nopt = parseOptionsOrDie(argc, argv, options);
	argc -= nopt;
	argv += nopt;
	struct SharedData* s;
	s = mapSharedDataOrDie(shm, O_RDWR);
	struct MagDataDyn magd;
	magDataDyn_map(&magd, s->mem);

	if (index != NULL) {
		if (argc == 0)
			return 0;
		int i = atoi(index);
		if (i >= magd.N)
			die("Lookup index too large\n");
		int fw = atoi(*argv);
		if (magd.active[i] != fw) {
			magd.active[i] = atoi(*argv);
			magDataDyn_populate(&magd);
		}
		return 0;
	}

	int i, fw, found, changed = 0, first_empty;
	while (argc-- > 0) {
		first_empty = -1;
		fw = atoi(*argv++);		
		found = 0;
		for (i = 0; i < magd.N; i++) {
			if (first_empty < 0 && magd.active[i] < 0)
				first_empty = i;
			if (magd.active[i] == fw) {
				found = 1;
				break;
			}
		}
		if (!found && first_empty >= 0) {
			magd.active[first_empty] = fw;
			changed = 1;
		}
	}
	if (changed)
		magDataDyn_populate(&magd);

	return 0;
}

static int cmdDeactivate(int argc, char **argv)
{
	char const* shm = defaultTargetShm;
	char const* index = NULL;
	struct Option options[] = {
		{"help", NULL, 0,
		 "deactivate [--shm=] <fwmarks...>\n"
		 "deactivate [--shm=] --index=#\n"
		 "  Deactivate targets"},
		{"shm", &shm, 0, "Shared memory"},
		{"index", &index, 0, "Index in the active table (<= N)"},
		{0, 0, 0, 0}
	};
	int nopt = parseOptionsOrDie(argc, argv, options);
	argc -= nopt;
	argv += nopt;
	struct SharedData* s;
	s = mapSharedDataOrDie(shm, O_RDWR);
	struct MagDataDyn magd;
	magDataDyn_map(&magd, s->mem);

	if (index != NULL) {
		int i = atoi(index);
		if (i >= magd.N)
			die("Lookup index too large\n");
		if (magd.active[i] >= 0) {
			magd.active[i] = -1;
			magDataDyn_populate(&magd);
		}
		return 0;
	}

	int changed = 0;
	while (argc-- > 0) {
		int fw = atoi(*argv++);		
		for (int i = 0; i < magd.N; i++) {
			if (magd.active[i] == fw) {
				magd.active[i] = -1;
				changed = 1;
				break;
			}
		}
	}
	if (changed)
		magDataDyn_populate(&magd);

	return 0;
}

__attribute__ ((__constructor__)) static void addCommands(void) {
	addCmd("activate", cmdActivate);
	addCmd("deactivate", cmdDeactivate);
}
