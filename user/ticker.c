// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// ticker [COUNT] [MILLISECONDS] -- prints a numbered tick every so often.
// Run a couple in the background to watch the scheduler interleave them.

#include "kunix.h"

int main(int argc, char** argv) {
    long count = argc > 1 ? atoi(argv[1]) : 5;
    long ms    = argc > 2 ? atoi(argv[2]) : 500;
    for (long i = 1; i <= count; i++) {
        printf("[ticker %d] tick %ld/%ld\n", getpid(), i, count);
        msleep((unsigned long)ms);
    }
    return 0;
}
