// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <types.h>
#include <console.h>
#include <klog.h>

// Enable Machine Check Exceptions (MCE), SSE for user programs, and SMEP.
void cpu_init(void) {
    uint64_t cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));

    cr4 |= (1 << 6);                 // MCE
    klog(LOG_OKAY, "Machine Check Exceptions enabled\n");

    // SSE: the kernel is built without it, but user programs may use it.
    // The scheduler saves and restores each process's registers (fxsave),
    // which is what OSFXSR promises the CPU we will do.
    cr0 &= ~(1ULL << 2);             // EM: no x87 emulation trap
    cr0 |= (1ULL << 1);              // MP: wait/fwait honours TS
    cr4 |= (1 << 9) | (1 << 10);     // OSFXSR, OSXMMEXCPT

    // SMEP: a kernel jump into a user page faults instead of running ring 3
    // code with ring 0 privilege. CPUID leaf 7, EBX bit 7.
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0), "c"(0));
    bool smep = false;
    if (a >= 7) {
        __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(7), "c"(0));
        smep = (b >> 7) & 1;
    }
    if (smep) cr4 |= (1 << 20);

    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));
    klog(LOG_OKAY, "SSE enabled for user programs%s\n", smep ? ", SMEP on" : "");
}