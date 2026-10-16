// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <types.h>

// This... copies stuff. Big shock. Eight bytes at a time (rep movsq),
// then the odd ones: the console moves megabytes with it.
void* memcpy(void* dest, const void* src, size_t n) {
    void* d = dest;
    size_t words = n / 8, rest = n % 8;
    __asm__ volatile("rep movsq" : "+D"(d), "+S"(src), "+c"(words) :: "memory");
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(src), "+c"(rest) :: "memory");
    return dest;
}

// The same, for regions that may overlap: backwards when the destination
// is above the source.
void* memmove(void* dest, const void* src, size_t n) {
    if (dest <= src || (const char*)dest >= (const char*)src + n) return memcpy(dest, src, n);
    void* d = (char*)dest + n - 1;
    const void* s = (const char*)src + n - 1;
    __asm__ volatile("std; rep movsb; cld" : "+D"(d), "+S"(s), "+c"(n) :: "memory");
    return dest;
}
