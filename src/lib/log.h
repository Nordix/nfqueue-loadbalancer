#pragma once
/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2022 Nordix Foundation
*/

#include <stdio.h>
#include <stdint.h>

#ifndef NO_LOG
struct LogConfig {
	int level;
	uint32_t tracemask;
};
extern struct LogConfig* logconfig;
extern FILE* logfile;

#define LOG_LEVEL logconfig->level
#define LOG_SET_LEVEL(x) logconfig->level = x
#define LOG_SET_TRACE_MASK(x) logconfig->tracemask = x

// Re-map the LogConfig to shared memory.
// This allows log and trace to be controlled in runtime.
void logConfigShm(char const* name);

// Start the trace server thread
void logTraceServer(char const* unix_socket);

#define WARNING if(logconfig->level>=4)
#define NOTICE if(logconfig->level>=5)
#define INFO if(logconfig->level>=6)
#define DEBUG if(logconfig->level>=7)
#define warning(arg...) WARNING{logp(arg);}
#define notice(arg...) NOTICE{logp(arg);}
#define info(arg...) INFO{logp(arg);}
#define debug(arg...) DEBUG{logp(arg);}
int logp(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

#define TRACE_LOG		1
#define TRACE(m) if((logconfig->tracemask & (m)) != 0)
#define trace(m,arg...) TRACE(m){tracef(arg);}
int tracef(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

#else
// Log/trace can be disabled for performance reasons/tests.
#define logConfigShm(x)
#define logTraceServer(x)
#define LOG_LEVEL -1
#define LOG_SET_LEVEL(x)
#define LOG_SET_TRACE_MASK(x)
#define WARNING if(0)
#define NOTICE if(0)
#define INFO if(0)
#define DEBUG if(0)
#define logp(arg...)
#define warning(arg...)
#define notice(arg...)
#define info(arg...)
#define debug(arg...)

#define TRACE(m) if(0)
#define tracef(arg...)
#define trace(m,arg...)
#endif
