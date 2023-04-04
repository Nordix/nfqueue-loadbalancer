/*
   SPDX-License-Identifier: Apache-2.0
   Copyright (c) 2021-2022 Nordix Foundation
*/

#include "shmem.h"
#include "die.h"
#include <string.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

// https://stackoverflow.com/questions/32683086/handling-incomplete-write-calls
// Modified to make a short delay between retries and return bytes written
// to conform with write(2).
static ssize_t
write_full(int fd, const void* buf, size_t size)
{
	ssize_t sz = size;
	ssize_t ret;
	while (sz > 0) {
		ret = write(fd, buf, sz);
		if (ret < 0) {
			if (errno == EINTR || errno == EAGAIN) {
				usleep(10000); // 10mS
				continue;
			}
			// A real error
			return ret;
		}
		sz -= ret;
		buf += ret;
	}
	return size;
}

int createSharedData(char const* name, void* data, size_t len)
{
	int fd = shm_open(name, O_RDWR|O_CREAT|O_TRUNC, 0600);
	if (fd < 0) return fd;
	int c = write_full(fd, data, len);
	if (c != len)
		return c;
	close(fd);
	return c;
}
void createSharedDataOrDie(char const* name, void* data, size_t len)
{
	if (createSharedData(name, data, len) != 0) {
		die("createSharedData: %s\n", strerror(errno));
	}
}
void* mapSharedData(char const* name, int mode)
{
	int fd = shm_open(name, mode, (mode == O_RDONLY)?0400:0600);
	if (fd < 0)
		return NULL;
	struct stat statbuf;
	if (fstat(fd, &statbuf) != 0)
		die("fstat shared mem; %s\n", name);
	void* m = mmap(
		NULL, statbuf.st_size,
		(mode == O_RDONLY)?PROT_READ:PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (m == MAP_FAILED) {
		close(fd);
		return NULL;
	}
	return m;
}
void* mapSharedDataOrDie(char const* name, int mode)
{
	void* m = mapSharedData(name, mode);
	if (m == NULL)
		die("FAILED mapSharedData: %s\n", name);
	return m;
}
void* mapSharedDataRead(char const* name, /*out*/int* _fd)
{
	int fd = shm_open(name, O_RDONLY, 0400);
	if (fd < 0)
		return NULL;
	struct stat statbuf;
	if (fstat(fd, &statbuf) != 0)
		die("fstat shared mem; %s\n", name);
	void* m = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (m == MAP_FAILED) {
		close(fd);
		return NULL;
	}
	*_fd = fd;
	return m;

}


