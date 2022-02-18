/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021-2022 Nordix Foundation
*/

#include <argv.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

static int cmpargv(char const** a, char const** b)
{
	while (*a != NULL && *b != NULL) {
		if (*a == NULL || *b == NULL)
			return -1;
		if (strcmp(*a, *b) != 0) {
			fprintf(stderr, "[%s] != [%s]\n", *a, *b);
			return -1;
		}
		a++;
		b++;
	}
	return 0;
}

int main(int argc, char* argv[])
{
	// Invalid params
	assert(mkargv(NULL, NULL) == NULL);
	assert(mkargv("x", NULL) == NULL);
	assert(mkargv("x", "") == NULL);
	assert(mkargv(NULL, ",") == NULL);
	assert(mkargv("", ",") == NULL);

	// Empty array
	assert(mkargv(",,,,,,", ",") == NULL);
	assert(mkargv(", ,  ,,,,", ", ") == NULL);

	// One-item array
	char const** a;
	a = mkargv(",,,,   item,,,", ", ");
	assert(a != NULL);
	assert(a[0] != NULL);
	assert(a[1] == NULL);
	assert(strcmp(a[0], "item") == 0);
	free(a);

	// Multi-item arrays
	char const* ex01[] = { "alpha", "beta", NULL };
	a = mkargv(", alpha, beta,, ", ", ");
	assert(a != NULL);
	assert(cmpargv(a, ex01) == 0);
	free(a);

	char const* ex02[] = { "1000::/64", "10.0.0.0/16", NULL };
	a = mkargv("1000::/64,10.0.0.0/16", ", ");
	assert(a != NULL);
	assert(cmpargv(a, ex02) == 0);
	free(a);

	char const* ex03[] = { "sctp[4:4] & 0xff00 = 0x4400", "tcp[0:2] = 55", NULL };
	a = mkargv("  sctp[4:4] & 0xff00 = 0x4400, tcp[0:2] = 55", ",");
	assert(a != NULL);
	assert(cmpargv(a, ex03) == 0);
	free(a);

	printf("=== argv-test OK\n");
	return 0;
}
