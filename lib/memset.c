// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <types.h>

// Fills eight bytes at a time (rep stosq), then the odd ones.
void memset(void* ptr, uint8_t value, uint32_t size) {
    uint64_t pattern = value * 0x0101010101010101ULL;
    uint64_t words = size / 8, rest = size % 8;
    __asm__ volatile("rep stosq" : "+D"(ptr), "+c"(words) : "a"(pattern) : "memory");
    __asm__ volatile("rep stosb" : "+D"(ptr), "+c"(rest) : "a"(pattern) : "memory");
}
