// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// Deliberately misbehaves, to check the kernel enforces segment permissions
// and survives a program that breaks the rules. There is no argv yet, so the
// test to perform is read from the keyboard after "run crash.elf".

#include "kunix.h"

static const char greeting[] = "constants live in a read-only segment";

int main(void) {
    kputs("crash tests:\n");
    kputs("  1  write to read-only data (.rodata)\n");
    kputs("  2  hand the kernel a pointer to kernel memory\n");
    kputs("  3  dereference NULL\n");
    kputs("pick one: ");

    char choice[8];
    long n = sys_read(KUNIX_STDIN, choice, sizeof(choice));
    if (n < 1) return 1;

    switch (choice[0]) {
        case '1':
            kputs("writing to .rodata...\n");
            *(volatile char*)greeting = 'C';           // must #PF
            break;
        case '2': {
            // Not a fault: the kernel must refuse the pointer and return -1.
            long r = sys_write(KUNIX_STDOUT, (const void*)0xFFFFFFFF80000000UL, 16);
            kputs(r < 0 ? "kernel refused the pointer, good.\n"
                        : "kernel accepted a kernel pointer!\n");
            return r < 0 ? 0 : 1;
        }
        case '3':
            kputs("dereferencing NULL...\n");
            return *(volatile long*)0;                 // must #PF
    }

    kputs("survived -- that should not happen\n");
    return 1;
}
