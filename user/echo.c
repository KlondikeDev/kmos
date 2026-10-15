// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// echo [-n] ARGS... -- the shell has a builtin; this is for exec'ing.

#include "kunix.h"

int main(int argc, char** argv) {
    int i = 1, newline = 1;
    if (argc > 1 && strcmp(argv[1], "-n") == 0) { newline = 0; i = 2; }
    for (; i < argc; i++) printf("%s%s", argv[i], i + 1 < argc ? " " : "");
    if (newline) printf("\n");
    return 0;
}
