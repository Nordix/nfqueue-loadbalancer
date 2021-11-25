#pragma once
/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

struct RangeSet;

// max=0 will set the default (256). An itempool is used so don't go crazy.
struct RangeSet* rangeSetCreate(unsigned max);
void rangeSetDestroy(struct RangeSet* t);
int rangeSetIn(struct RangeSet* t, unsigned value);

/*
  The added ranges are not inserted until the RangeSet is updated. On
  update overlapping ranges are merged.
 */
int rangeSetAdd(struct RangeSet* t, unsigned first, unsigned last);
int rangeSetAddStr(struct RangeSet* t, char const* str);
void rangeSetUpdate(struct RangeSet* t);

// Returns the total number of items (including added but not updated)
unsigned rangeSetSize(struct RangeSet* t);

/*
  Returns;
   0 - Ok
   1 - Can't fit in the passed string
  -1 - Fail
 */
int rangeSetString(struct RangeSet* t, char* str, unsigned len);
