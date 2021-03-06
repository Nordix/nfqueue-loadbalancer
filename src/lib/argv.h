#pragma once
/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021-2022 Nordix Foundation
*/

// Create a NULL-terminated string array from a string.
// Quotes and escape chars are not supported. mkargv() is thread safe.
// str and delim must be !=NULL and !="".
// NOTE: The caller must free() the returned array.
// Return;
//  NULL - If there are no items or if params are invalid.
//  NULL-terminated argv array containing at least one item.
char const** mkargv(char const* str, const char *delim);
