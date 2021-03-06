/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021-2022 Nordix Foundation
*/

#include "argv.h"
#include <die.h>
#include <string.h>
#include <stdlib.h>

char const** mkargv(char const* str, const char *delim)
{
	if (str == NULL || delim == NULL || *str == 0 || *delim == 0)
		return NULL;

	// Pass 1; compute the array length
	unsigned len = 0;
	char* tmp = strdup(str);
	if (tmp == NULL) die("OOM");
	char* saveptr;
	char* tok = strtok_r(tmp, delim, &saveptr);
	while (tok != NULL) {
		len++;
		tok = strtok_r(NULL, delim, &saveptr);
	}
	free(tmp);
	if (len == 0)
		return NULL;

	// Pass 2; create the array
	// The array (char**) and the strings are in the same memory-block
	// so the caller can call free() on the returned value.
	unsigned slen = strlen(str) + 1; /* include the '\0' */
	void* buf = malloc(sizeof(char*) * (len+1) + slen);
	if (buf == NULL) die("OOM");

	char const** arg = buf;
	tmp = (char*)(arg + (len+1));
	memcpy(tmp, str, slen);
	tok = strtok_r(tmp, delim, &saveptr);
	while (tok != NULL) {
		// Trim leading spaces
		while (*tok != 0 && *tok == ' ')
			tok++;
		*arg++ = tok;
		tok = strtok_r(NULL, delim, &saveptr);
	}
	*arg = NULL;

	return buf;
}

