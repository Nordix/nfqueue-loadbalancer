/*
  SPDX-License-Identifier: MIT License
  Copyright (c) 2021 Nordix Foundation
*/

#include <stdint.h>

struct ip6_hdr;
struct icmp6_hdr;
unsigned ipv4TcpUdpHash(void const* data, unsigned len);
unsigned ipv4IcmpHash(void const* data, unsigned len);
unsigned ipv4AddressHash(void const* data, unsigned len);
int ipv4HandleFragment(void const* data, unsigned len, unsigned* hash);
unsigned ipv6Hash(void const* data, unsigned len);
unsigned ipv6AddressHash(void const* data, unsigned len);
int ipv6HandleFragment(void const* data, unsigned len, unsigned* hash);

// MAC
int macParse(char const* str, uint8_t* mac);
void macParseOrDie(char const* str, uint8_t* mac);
char const* macToString(uint8_t const* mac);
int getMAC(char const* iface, /*out*/ unsigned char* mac);

// Print
void ipv4Print(unsigned len, uint8_t const* pkt);
void ipv6Print(unsigned len, uint8_t const* pkt);
void framePrint(unsigned len, uint8_t const* pkt);

// Csum (only ipv4 for now)
void tcpCsum(uint8_t* pkt, unsigned len);

