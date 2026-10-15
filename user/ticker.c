// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// Prints a numbered tick every so often. Run a couple in the background
// ("ticker 5 300 &") to watch the scheduler interleave them.
//
//   ticker [COUNT] [MILLISECONDS]

#include "kunix.h"

static long parse(const char* s, long fallback) {
    if (!s || !*s) return fallback;
    long v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return fallback;
        v = v * 10 + (*s - '0');
    }
    return v;
}

int main(int argc, char** argv) {
    long count = parse(argc > 1 ? argv[1] : 0, 5);
    long ms    = parse(argc > 2 ? argv[2] : 0, 500);
    long pid   = sys_getpid();

    for (long i = 1; i <= count; i++) {
        kputs("[ticker ");
        kputi(pid);
        kputs("] tick ");
        kputi(i);
        kputs("/");
        kputi(count);
        kputs("\n");
        sys_sleep((u64)ms);
    }
    return 0;
}
