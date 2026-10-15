// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// touch FILE... -- create files that do not exist (no timestamps yet).

#include "kunix.h"

int main(int argc, char** argv) {
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_WRONLY | O_CREAT, 0644);
        if (fd < 0) { dprintf(STDERR_FILENO, "touch: %s: %s\n", argv[i], strerror(errno)); rc = 1; continue; }
        close(fd);
    }
    return rc;
}
