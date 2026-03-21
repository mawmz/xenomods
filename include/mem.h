/**
 * @file mem.h
 * @brief Memory functions.
 */

#pragma once

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

void* memset(void* src, int val, u64 num);
void* memcpy(void* dest, void const* src, u64 count);
void* memmove(void* dest, const void* src, u64 count);
void* memalign(size_t alignment, size_t size);

#ifdef __cplusplus
}
#endif