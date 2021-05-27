/*
  SPDX-License-Identifier: MIT License
  Copyright (c) 2021 Nordix Foundation
*/

#include <time.h>

struct limiter;
struct limiter* limiterCreate(unsigned count, unsigned intervalMillis);
int limiterGo(struct timespec* now, struct limiter* l);
