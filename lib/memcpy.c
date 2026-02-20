// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <types.h>

void* memcpy(void* dest, const void* src, size_t n) {
    char* csrc = (char*)src;
    char* cdest = (char*)dest;
    for (size_t i = 0; i < n; i++) {
        cdest[i] = csrc[i];
    }
    return dest;
}
