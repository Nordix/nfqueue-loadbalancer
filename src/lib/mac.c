/*
  SPDX-License-Identifier: MIT License
  Copyright (c) 2021 Nordix Foundation
*/

#include "die.h"
#include "iputils.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <netinet/ether.h>

int macParse(char const* str, uint8_t* mac)
{
	int values[6];
	int i = sscanf(
		str, "%x:%x:%x:%x:%x:%x%*c",
		&values[0], &values[1], &values[2],
		&values[3], &values[4], &values[5]);

	if (i == 6) {
		/* convert to uint8_t */
		for( i = 0; i < 6; ++i )
			mac[i] = (uint8_t) values[i];
		return 0;
	}
    /* invalid mac */
	return -1;
}
void macParseOrDie(char const* str, uint8_t* mac)
{
	if (macParse(str, mac) != 0)
		die("Parse MAC failed [%s]\n", str);
}

char const* macToString(uint8_t const* mac)
{
#define MAX_MACBUF 2
	static char buf[MAX_MACBUF][20];
	static int bindex = 0;
	if (bindex++ == MAX_MACBUF) bindex = 0;
	sprintf(
		buf[bindex], "%02x:%02x:%02x:%02x:%02x:%02x",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return buf[bindex];
}

#include <sys/ioctl.h>
#include <net/if.h>	//ifreq

int getMAC(char const* iface, /*out*/ unsigned char* mac)
{
	int fd;
	struct ifreq ifr;
	
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) return -1;
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name , iface , IFNAMSIZ-1);
	if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
		close(fd);
		return -1;
	}
	close(fd);
	memcpy(mac, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
	return 0;
}

