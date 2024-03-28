#include "nfqlb.h"
#include <string.h>
#include <assert.h>

static char const* const req001 =
	"action:set\n"
	"name:kalle\n"
	"eoc:\n";
static char const* const req002 =
	"dsts:D1::, D2:: D3::,D4::\n"
	"eoc:\n";
static char const* const req003 =
	"action:set\n"
	"name:kalle\n";
static char const* const req004 =
	"action:set\n"
	"name, kalle\n"
	"eoc:\n";

static int cmpArgv(char const** argv1, char const* argv2[])
{
	while (*argv1 != NULL && *argv2 != NULL) {
		//printf("=== %s %s\n", *argv1, *argv2);
		if (strcmp(*argv1++, *argv2++) != 0)
			return 1;
	}
    return (*argv1 == NULL && *argv2 == NULL) ? 0 : 1;
}

int main(int argc, char* argv[])
{
	struct FlowCmd cmd = {0};
	FILE* in;

	// Free an empty cmd should work
	freeFlowCmd(&cmd);

	// Normal stings
	in = fmemopen((void*)req001, strlen(req001), "r");
	assert(readFlowCmd(in, &cmd) == 0);
	assert(strcmp(cmd.action, "set") == 0);
	assert(strcmp(cmd.name, "kalle") == 0);
	assert(cmd.target == NULL);
	fclose(in);
	freeFlowCmd(&cmd);
	assert(cmd.action == NULL);

	// Argv's
	in = fmemopen((void*)req002, strlen(req002), "r");
	assert(readFlowCmd(in, &cmd) == 0);
	fclose(in);
	assert(cmd.dsts != NULL);
	char const* expa[] = {"D1::","D2::","D3::","D4::"};
	assert(cmpArgv(cmd.dsts, expa) == 0);
	freeFlowCmd(&cmd);
	assert(cmd.dsts == NULL);

	// No terminating "eoc:"
	in = fmemopen((void*)req003, strlen(req003), "r");
	assert(readFlowCmd(in, &cmd) != 0);
	fclose(in);
	assert(cmd.action == NULL);

	// Invalid parameter
	in = fmemopen((void*)req004, strlen(req004), "r");
	assert(readFlowCmd(in, &cmd) != 0);
	fclose(in);
	assert(cmd.action == NULL);

	printf("=== remoteCmd-test OK\n");
	return 0;
}
