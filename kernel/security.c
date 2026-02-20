// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <panic.h>
#include <console.h>
#include <klog.h>
#include <security.h>

// Canary!
uint64_t __stack_chk_guard = 0xCA5CADECA5CADEAA;

void watchdog_violation(const char* reason) {
    terminal_printf(WHITE, BLACK, "WATCHDOG VIOLATION:\n");
    klog(LOG_ERROR, "%s\n", reason);
    terminal_printf(BLACK, WHITE, "This instance of the Kunix Millennium Operating System may be compromised. Please reboot your system.\n");
    
    __asm__ volatile("cli");
    while(1) {
        __asm__ volatile("hlt");
    }
}

void __stack_chk_fail(void) {
    watchdog_violation("STACK SMASHING DETECTED");
}

void test_ssp(void) {
    volatile char buffer[8];
    volatile char* ptr = buffer;
    
    for (volatile int i = 0; i < 64; i++) {
        ptr[i] = 'A';  // No warning with pointer access
    }
    
}