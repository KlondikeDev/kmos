// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>

#define MSR_EFER         0xC0000080
#define MSR_STAR         0xC0000081   // syscall/sysret segment selectors
#define MSR_LSTAR        0xC0000082   // 64-bit syscall entry point
#define MSR_FMASK        0xC0000084   // RFLAGS bits cleared on syscall
#define MSR_FS_BASE      0xC0000100
#define MSR_GS_BASE      0xC0000101

#define EFER_SCE (1ULL << 0)          // syscall/sysret enable
#define EFER_NXE (1ULL << 11)

static inline u64 rdmsr(u32 msr) {
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return (u64)hi << 32 | lo;
}

static inline void wrmsr(u32 msr, u64 value) {
    __asm__ volatile("wrmsr" :: "c"(msr), "a"((u32)value), "d"((u32)(value >> 32)));
}
