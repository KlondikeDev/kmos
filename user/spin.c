// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// spin [ROUNDS] -- burns CPU without ever blocking, then reports. Two of
// these in the background finish together only if the timer preempts ring 3.

#include "kunix.h"

int main(int argc, char** argv) {
    long rounds = argc > 1 ? atoi(argv[1]) : 20;
    volatile unsigned char* buf = malloc(64 * 1024);
    if (!buf) { puts("spin: out of memory"); return 1; }
    for (long r = 1; r <= rounds; r++) {
        for (unsigned long i = 0; i < 64 * 1024; i++) buf[i] = (unsigned char)(buf[i] + i + r);
        for (volatile unsigned long k = 0; k < 2000000; k++) { }
        if (r % 5 == 0) printf("[spin %d] round %ld\n", getpid(), r);
    }
    return 0;
}
