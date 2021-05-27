/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

#include "limiter.h"
#include <stdlib.h>
#include <stdint.h>

// Limiter
struct limiter {
	unsigned intervalCount;
	unsigned intervalMillis;
	unsigned count;
	uint64_t lastGoMillis;
};
struct limiter* limiterCreate(unsigned intervalCount, unsigned intervalMillis)
{
	struct limiter* l = calloc(1,sizeof(*l));
	l->intervalCount = intervalCount;
	l->intervalMillis = intervalMillis;
	return l;
}
int limiterGo(struct timespec* now, struct limiter* l)
{
	uint64_t nowMillis = now->tv_sec * 1000 + now->tv_nsec / 1000000;
	l->count++;
	if ((nowMillis - l->lastGoMillis) >= l->intervalMillis
		|| l->count >= l->intervalCount) {
		l->lastGoMillis = nowMillis;
		l->count = 0;
		return 1;
	}
	return 0;
}
