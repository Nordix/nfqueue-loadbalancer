#pragma once
/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

#include <conntrack.h>
#include <stdio.h>

struct FlowSet;
struct FlowSet* flowSetCreate(void (*lock_user_ref)(void* user_ref));
void flowSetDelete(struct FlowSet* set);
unsigned flowSetSize(struct FlowSet* set);

// Add or replace a flow
// Returns 0 on success, !=0 otherwise.
int flowDefine(
	struct FlowSet* set,
	char const* name,
	int priority,
	void* user_ref,
	char const* protocols[],
	char const* dports,
	char const* sports,
	char const* dsts[],
	char const* srcs[],
	unsigned short udpencap);	/* In host byte order! */

// Delete a flow.
// If the flow exists the "user_ref" is returned and udpencap is set.
// If the flow doesn't exist this is a no-op and NULL is returned.
void* flowDelete(
	struct FlowSet* set, char const* name, /*out*/unsigned short* udpencap);

// Lookup a key.
// If a lock_user_ref() function is defined it will be called while
// the set is locked. It shall be used to ensure that the user_ref is not
// deleted while in use.
// Returns the "user_ref" the key matches a flow, NULL if not.
void* flowLookup(
	struct FlowSet* set,
	struct ctKey* key,
	/*out*/unsigned short* udpencap);

// Print a flow if name != NULL or the entire set.
// Output is in json format.
void flowSetPrint(
	FILE* out, struct FlowSet* set, char const* name,
	char const* (*user_ref2string)(void* user_ref));
