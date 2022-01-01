/*
   SPDX-License-Identifier: Apache-2.0
   Copyright (c) 2021-2022 Nordix Foundation
*/

#include "die.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

void die(char const* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	exit(EXIT_FAILURE);
}
