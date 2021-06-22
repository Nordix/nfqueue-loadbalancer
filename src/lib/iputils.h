#pragma once
/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

#define IN_BOUNDS(p,o,e) (((void const*)p + o) < (void const*)(e))

unsigned ipv4Hash(void const* data, unsigned len);

// Prerequisite; The header fits in the packet (IN_BOUNDS)
unsigned ipv4AddressHash(void const* data, unsigned len);

unsigned ipv6Hash(
	void const* data, unsigned len, unsigned htype, void const* hdr);

// Prerequisite; The header fits in the packet (IN_BOUNDS)
unsigned ipv6AddressHash(void const* data, unsigned len);

int ipv6IsExtensionHeader(unsigned htype);
