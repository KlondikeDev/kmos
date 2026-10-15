// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// cat [FILE...] -- copy files (or stdin, or "-") to stdout.

#include "kunix.h"

static int copy(int fd, const char* name) {
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write(STDOUT_FILENO, buf, (size_t)n) < 0) return 1;  // e.g. EPIPE
    }
    if (n < 0) {
        dprintf(STDERR_FILENO, "cat: %s: %s\n", name, strerror(errno));
        return 1;
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) return copy(STDIN_FILENO, "stdin");
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) { rc |= copy(STDIN_FILENO, "stdin"); continue; }
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            dprintf(STDERR_FILENO, "cat: %s: %s\n", argv[i], strerror(errno));
            rc = 1;
            continue;
        }
        rc |= copy(fd, argv[i]);
        close(fd);
    }
    return rc;
}
