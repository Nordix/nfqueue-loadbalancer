#pragma once
/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

#include <stdint.h>
#include "fragutils.h"

unsigned ipv4TcpUdpHash(void const* data, unsigned len);
unsigned ipv4IcmpHash(void const* data, unsigned len);
unsigned ipv4AddressHash(void const* data, unsigned len);
unsigned ipv6Hash(void const* data, unsigned len);
unsigned ipv6AddressHash(void const* data, unsigned len);
typedef void (*injectFragFn_t)(void const* data, unsigned len);
int ipv6HandleFragment(
	struct FragTable* ft, void const* data, unsigned len, unsigned* hash,
	injectFragFn_t injectFragFn);
int ipv4HandleFragment(
	struct FragTable* ft, void const* data, unsigned len, unsigned* hash,
	injectFragFn_t injectFragFn);

