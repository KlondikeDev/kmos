// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// crash N -- misbehave on purpose, to check the kernel contains it.
//   1  write to read-only data      (SIGSEGV)
//   2  hand the kernel a kernel pointer   (must get EFAULT, not crash)
//   3  dereference NULL              (SIGSEGV)
//   4  execute a privileged instruction  (SIGSEGV via #GP)
//   5  divide by zero                (SIGFPE)

#include "kunix.h"

static const char greeting[] = "constants live in a read-only segment";

int main(int argc, char** argv) {
    int which = argc > 1 ? atoi(argv[1]) : 0;
    switch (which) {
        case 1: *(volatile char*)greeting = 'C'; break;
        case 2: {
            ssize_t r = write(STDOUT_FILENO, (const void*)0xFFFFFFFF80000000UL, 16);
            printf("write(kernel pointer) = %ld, errno %d (%s)\n", (long)r, errno, strerror(errno));
            return r < 0 && errno == EFAULT ? 0 : 1;
        }
        case 3: return *(volatile int*)0;
        case 4: __asm__ volatile("cli"); break;
        case 5: { volatile int z = 0; return 10 / z; }
        default:
            puts("usage: crash 1|2|3|4|5");
            return 2;
    }
    puts("survived -- that should not happen");
    return 1;
}
