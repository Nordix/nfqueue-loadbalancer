/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021-2022 Nordix Foundation
*/

#include <iputils.h>

#include <stdio.h>
#include <assert.h>
#include <sys/un.h>
#include <stddef.h>
#include <netinet/in.h>

int main(int argc, char* argv[])
{
	struct sockaddr_storage sas;
	struct sockaddr_un const* saU = (struct sockaddr_un const*)&sas;
	struct sockaddr_in const* sa4 = (struct sockaddr_in const*)&sas;
	struct sockaddr_in6 const* sa6 = (struct sockaddr_in6 const*)&sas;
	socklen_t len;

	assert(parseAddress("unix:nfqlb", &sas, &len) == 0);
	assert(sas.ss_family == AF_UNIX);
	assert(saU->sun_path[0] == 0);
	assert(strcmp(saU->sun_path + 1, "nfqlb") == 0);
	socklen_t exlen =
		offsetof(struct sockaddr_un, sun_path) + strlen(saU->sun_path+1) + 1;
	//printf("=== %u %u\n", len, exlen);
	assert(len == exlen);

	// ANY address
	assert(parseAddress("tcp:0.0.0.0:23", &sas, &len) == 0);
	assert(sas.ss_family == AF_INET);
	assert(htons(sa4->sin_port) == 23);
	assert(sa4->sin_addr.s_addr == INADDR_ANY);
	assert(len == sizeof(struct sockaddr_in));

	assert(parseAddress("tcp:[::]:23", &sas, &len) == 0);
	assert(sas.ss_family == AF_INET6);
	assert(htons(sa6->sin6_port) == 23);
	assert(IN6_IS_ADDR_UNSPECIFIED(&sa6->sin6_addr));

	// LOOPBACK address
	assert(parseAddress("tcp:127.0.0.1:8080", &sas, &len) == 0);
	assert(sas.ss_family == AF_INET);
	assert(htons(sa4->sin_port) == 8080);
	//printf("=== %08x %08x\n", sa4->sin_addr.s_addr, INADDR_LOOPBACK);
	assert(ntohl(sa4->sin_addr.s_addr) == INADDR_LOOPBACK);

	assert(parseAddress("tcp:[::1]:443", &sas, &len) == 0);
	assert(sas.ss_family == AF_INET6);
	assert(htons(sa6->sin6_port) == 443);
	assert(IN6_IS_ADDR_LOOPBACK(&sa6->sin6_addr));

	// IPv6 link-local
	assert(parseAddress("tcp:[fe80::1%lo]:443", &sas, &len) == 0);
	assert(sas.ss_family == AF_INET6);
	assert(htons(sa6->sin6_port) == 443);
	//printf("=== %u\n", sa6->sin6_scope_id);
	assert(sa6->sin6_scope_id != 0);

	// Some fault tests;
	assert(parseAddress("udp:[::]:443", &sas, &len) != 0);
	assert(parseAddress("tcp:[::]:80000", &sas, &len) != 0);
	assert(parseAddress("tcp:[::]:-4", &sas, &len) != 0);
	assert(parseAddress("tcp:100.0.0.256:0", &sas, &len) != 0);
	assert(parseAddress("tcp:[2000::1::1]:0", &sas, &len) != 0);

	printf("=== iputils-test OK\n");
	return 0;
}

