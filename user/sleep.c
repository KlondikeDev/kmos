// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// sleep SECONDS -- whole seconds, or with a fraction: sleep 0.25

#include "kunix.h"

int main(int argc, char** argv) {
    if (argc < 2) { dprintf(STDERR_FILENO, "usage: sleep SECONDS\n"); return 2; }
    unsigned long ms = 0;
    const char* s = argv[1];
    while (isdigit(*s)) ms = ms * 10 + (unsigned long)(*s++ - '0');
    ms *= 1000;
    if (*s == '.') {
        unsigned long scale = 100;
        for (s++; isdigit(*s) && scale; s++, scale /= 10) ms += (unsigned long)(*s - '0') * scale;
    }
    return msleep(ms) < 0 ? 1 : 0;
}
