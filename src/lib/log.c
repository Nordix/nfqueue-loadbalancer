/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2022 Nordix Foundation
*/
#ifndef NO_LOG

#include <log.h>
#include <die.h>
#include <shmem.h>
#include <iputils.h>
#include <pthread.h>
#include <unistd.h>
#include <stdarg.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

static struct LogConfig default_config = {5, 0};
struct LogConfig* logconfig = &default_config;
FILE* logfile = NULL;
static FILE* tracefile = NULL;

static pthread_mutex_t tlock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&tlock)
#define UNLOCK() pthread_mutex_unlock(&tlock)

int tracef(const char *fmt, ...)
{
	LOCK();
	if (tracefile == NULL) {
		UNLOCK();
		return 0;
	}
	va_list ap;
	va_start(ap, fmt);
	int rc = vfprintf(tracefile, fmt, ap);
	va_end(ap);
	UNLOCK();
	return rc;
}

int logp(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int rc = vfprintf(logfile, fmt, ap);
	va_end(ap);

	if (tracefile == NULL || (logconfig->tracemask & TRACE_LOG) == 0)
		return rc;

	// Log trace is active
	LOCK();
	if (tracefile == NULL) {
		UNLOCK();
		return 0;
	}
	va_start(ap, fmt);
	vfprintf(tracefile, fmt, ap);
	va_end(ap);
	UNLOCK();
	return rc;
}



void logConfigShm(char const* name)
{
	logconfig = mapSharedData(name, O_RDWR, NULL);
	if (logconfig != NULL)
		return;
	createSharedDataOrDie(name, &default_config, sizeof(struct LogConfig));
	logconfig = mapSharedDataOrDie(name, O_RDWR, NULL);
}

__attribute__ ((__constructor__)) static void loginit(void) {
	logfile = stderr;
	tracefile = stderr;
}


static void* traceServerThread(void* arg)
{
	info("Started Trace\n");
	struct sockaddr_storage sa;
	socklen_t len;
	char const* addr = arg;
	if (parseAddress(addr, &sa, &len) != 0)
		die("Failed to parse address [%s]", addr);
	int sd = socket(sa.ss_family, SOCK_STREAM, 0);
	if (sd < 0)
		die("Trace server socket: %s\n", strerror(errno));
	if (bind(sd, (struct sockaddr*)&sa, len) != 0)
		die("Trace server bind: %s\n", strerror(errno));
	if (listen(sd, 1) != 0)
		die("Trace server listen: %s\n", strerror(errno));
	for (;;) {
		debug("Trace: accept\n");
		int cd = accept(sd, NULL, NULL);
		info("Trace; Accepted incoming connection. cd=%d\n", cd);
		if (cd < 0) {
			warning("Trace: accept returns %d\n", cd);
			continue;
		}
		LOCK();
		tracefile = fdopen(cd, "w");
		if (tracefile == NULL) {
			UNLOCK();
			warning("Trace: fdopen failed\n");
			close(cd);
			continue;
		}
		setlinebuf(tracefile);
		UNLOCK();

		warning("Trace; active mask=0x%08x\n", logconfig->tracemask);
		tracef("Trace active, mask=0x%08x\n", logconfig->tracemask);

		// Any input should be a shutdown
		signal(SIGPIPE, SIG_IGN); /* (see comment below) */
		char buffer[254];
		(void)read(cd, buffer, sizeof(buffer));
		/*
		  Precisely *here* the "cd" file descriptor is closed by the peer.
		  If a thread writes to "tracefile" a SIGPIPE is generated.
		  The default action is to terminate the process. Not good!
		*/
		tracef("SIGPIPE TEST\n");
		logconfig->tracemask = 0;

		LOCK();
		fclose(tracefile);
		tracefile = NULL;
		close(cd);
		UNLOCK();
		signal(SIGPIPE, SIG_DFL);
		warning("Trace: closed\n");
	}
	return NULL;
}

void logTraceServer(char const* unix_socket) {
	pthread_t tid;
	if (pthread_create(&tid, NULL, traceServerThread, (void*)unix_socket) != 0)
		die("\n");
}

#endif
