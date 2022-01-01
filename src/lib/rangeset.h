#pragma once
/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021-2022 Nordix Foundation
*/

/*
  Implements a set of integer (port) ranges.
  Delete operations are not supported.
 */

struct RangeSet;

struct RangeSet* rangeSetCreate(void);
struct RangeSet* rangeSetCreateLimited(unsigned lowest, unsigned highest);
void rangeSetDestroy(struct RangeSet* t);

// Returns true (!=0) if the value is in the set.
int rangeSetIn(struct RangeSet* t, unsigned value);

/*
  The added ranges are not inserted until the RangeSet is updated. On
  update overlapping ranges are merged. rangeSetAddStr() accepts
  comma (or space) separated values, example; "0,222-333, 55".
  rangeSetAddArgv() accepts a null terminated array of strings.
  Returns 0 on success, -1 on failure.
 */
int rangeSetAdd(struct RangeSet* t, unsigned first, unsigned last);
int rangeSetAddStr(struct RangeSet* t, char const* str);
int rangeSetAddArgv(struct RangeSet* t, char const* argv[]);
void rangeSetUpdate(struct RangeSet* t);

// Returns the total number of items (including added but not updated)
unsigned rangeSetSize(struct RangeSet* t);

/*
  Prints the set as comma-separated values (same as used in rangeSetAddStr()).
  Returns;
   0 - Ok
   1 - Can't fit in the passed string
  -1 - Fail
 */
int rangeSetString(struct RangeSet* t, char* str, unsigned len);
