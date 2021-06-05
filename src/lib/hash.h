#pragma once
/*
  http://www.cse.yorku.ca/~oz/hash.html
*/

#include <stdint.h>

#ifndef HASH
#define HASH(d,l) djb2_hash((uint8_t const*)d,(uint32_t)l)
#endif

uint32_t djb2_hash(uint8_t const* c, uint32_t len);
