// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// rmdir DIR... -- remove empty directories.

#include "kunix.h"

int main(int argc, char** argv) {
    if (argc < 2) { dprintf(STDERR_FILENO, "usage: rmdir DIR...\n"); return 2; }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (rmdir(argv[i]) < 0) {
            dprintf(STDERR_FILENO, "rmdir: %s: %s\n", argv[i], strerror(errno));
            rc = 1;
        }
    }
    return rc;
}
