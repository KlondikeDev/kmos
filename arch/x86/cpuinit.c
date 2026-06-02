// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <types.h>
#include <console.h>
#include <klog.h>

// Enable Machine Check Exceptions (MCE)
void cpu_init(void) {
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 6);
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));

    klog(LOG_OKAY, "Machine Check Exceptions enabled\n");
}