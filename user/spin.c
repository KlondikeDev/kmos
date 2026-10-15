// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// Burns CPU without ever making a syscall, then reports. Run two of these
// in the background: if they finish together rather than one after the
// other, the timer is preempting ring 3 -- nobody is yielding voluntarily.
// Also exercises the heap: the work buffer comes from sbrk.
//
//   spin [ROUNDS]

#include "kunix.h"

int main(int argc, char** argv) {
    long rounds = 20;
    if (argc > 1) {
        rounds = 0;
        for (const char* s = argv[1]; *s >= '0' && *s <= '9'; s++) rounds = rounds * 10 + (*s - '0');
    }

    volatile unsigned char* buf = kalloc(64 * 1024);
    if (!buf) {
        kputs("spin: sbrk failed\n");
        return 1;
    }

    long pid = sys_getpid();
    for (long r = 1; r <= rounds; r++) {
        for (u64 i = 0; i < 64 * 1024; i++) buf[i] = (unsigned char)(buf[i] + i + r);
        for (volatile u64 k = 0; k < 2000000; k++) { }
        if (r % 5 == 0) {
            kputs("[spin ");
            kputi(pid);
            kputs("] round ");
            kputi(r);
            kputs("\n");
        }
    }
    return 0;
}
