#pragma once
/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

unsigned ipv4Hash(void const* data, unsigned len);
unsigned ipv4AddressHash(void const* data, unsigned len);

unsigned ipv6Hash(
	void const* data, unsigned len, unsigned htype, void const* hdr);
unsigned ipv6AddressHash(void const* data, unsigned len);
int ipv6IsExtensionHeader(unsigned htype);

