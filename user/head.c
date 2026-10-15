// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// head [-n N] [FILE] -- the first N (default 10) lines.

#include "kunix.h"

int main(int argc, char** argv) {
    long want = 10;
    int i = 1;
    if (i + 1 < argc && strcmp(argv[i], "-n") == 0) { want = atoi(argv[i + 1]); i += 2; }
    int fd = STDIN_FILENO;
    if (i < argc) {
        fd = open(argv[i], O_RDONLY);
        if (fd < 0) { dprintf(STDERR_FILENO, "head: %s: %s\n", argv[i], strerror(errno)); return 1; }
    }
    char buf[4096];
    long lines = 0;
    ssize_t n;
    while (lines < want && (n = read(fd, buf, sizeof(buf))) > 0) {
        ssize_t upto = 0;
        while (upto < n && lines < want) if (buf[upto++] == '\n') lines++;
        write(STDOUT_FILENO, buf, (size_t)upto);
    }
    return 0;
}
