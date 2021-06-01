#pragma once
/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

#include <stdint.h>
#include "fragutils.h"
struct ip6_hdr;
struct icmp6_hdr;
unsigned ipv4TcpUdpHash(void const* data, unsigned len);
unsigned ipv4IcmpHash(void const* data, unsigned len);
unsigned ipv4AddressHash(void const* data, unsigned len);
unsigned ipv6Hash(void const* data, unsigned len);
unsigned ipv6TcpUdpHash(struct ip6_hdr const* h, uint32_t const* ports);
unsigned ipv6IcmpHash(struct ip6_hdr const* h, struct icmp6_hdr const* ih);
unsigned ipv6AddressHash(void const* data, unsigned len);

typedef void (*injectFragFn_t)(void const* data, unsigned len);
int ipv6HandleFragment(
	struct FragTable* ft, void const* data, unsigned len, unsigned* hash,
	injectFragFn_t injectFragFn);
int ipv4HandleFragment(
	struct FragTable* ft, void const* data, unsigned len, unsigned* hash,
	injectFragFn_t injectFragFn);

