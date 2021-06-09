#pragma once
#include <conntrack.h>

struct SharedData {
	int ownFwmark;
	unsigned char mem[];
};

extern char const* const defaultTargetShm;

typedef int (*packetHandleFn_t)(
	unsigned short proto, void* payload, unsigned plen);
void nfqueueInit(
	packetHandleFn_t packetHandleFn, unsigned queue_length, unsigned mtu);
int nfqueueRun(unsigned int queue_num); /* Will not return */

