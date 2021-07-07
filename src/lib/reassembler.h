/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

#include <fragutils.h>
#include <itempool.h>

/*
  Size is the number of "holes" handled by the reassembler defined in;
  https://datatracker.ietf.org/doc/html/rfc815
 */
struct FragReassembler* createReassembler(unsigned int size);

struct ReassemblerStats {
	unsigned assembled;
	struct ItemPoolStats const* pool;
};
struct ReassemblerStats const* getReassemblerStats(void);

