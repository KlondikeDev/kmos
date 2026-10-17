// This file has been generated in whole or in part by a Large Language Model
#pragma once
#include <types.h>
#define RANDOM_HW_RDRAND 1u
#define RANDOM_HW_RDSEED 2u
static inline u32 random_hw_features(void) {
    u32 a = 0, b, c, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    u32 maximum = a, features = 0;
    if (maximum >= 1) {
        a = 1;
        __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
        if (c & (1u << 30)) features |= RANDOM_HW_RDRAND;
    }
    if (maximum >= 7) {
        a = 7; c = 0;
        __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
        if (b & (1u << 18)) features |= RANDOM_HW_RDSEED;
    }
    return features;
}
static inline bool random_hw_word(bool seed, u64* out) {
    u8 carry;
    if (seed) __asm__ volatile("rdseed %0; setc %1" : "=r"(*out), "=qm"(carry) :: "cc");
    else __asm__ volatile("rdrand %0; setc %1" : "=r"(*out), "=qm"(carry) :: "cc");
    return carry != 0;
}
