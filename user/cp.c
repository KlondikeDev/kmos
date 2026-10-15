// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// cp SRC DST -- copy one file. DST may be a directory.

#include "kunix.h"

int main(int argc, char** argv) {
    if (argc != 3) { dprintf(STDERR_FILENO, "usage: cp SRC DST\n"); return 2; }
    const char* src = argv[1];
    char dst[256];
    struct stat st;
    if (stat(argv[2], &st) == 0 && S_ISDIR(st.st_mode)) {
        const char* base = strrchr(src, '/');
        snprintf(dst, sizeof(dst), "%s/%s", argv[2], base ? base + 1 : src);
    } else {
        strncpy(dst, argv[2], sizeof(dst) - 1);
        dst[sizeof(dst) - 1] = '\0';
    }

    int in = open(src, O_RDONLY);
    if (in < 0) { dprintf(STDERR_FILENO, "cp: %s: %s\n", src, strerror(errno)); return 1; }
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { dprintf(STDERR_FILENO, "cp: %s: %s\n", dst, strerror(errno)); return 1; }

    char buf[4096];
    ssize_t n;
    while ((n = read(in, buf, sizeof(buf))) > 0) {
        if (write(out, buf, (size_t)n) != n) { dprintf(STDERR_FILENO, "cp: %s: %s\n", dst, strerror(errno)); return 1; }
    }
    if (n < 0) { dprintf(STDERR_FILENO, "cp: %s: %s\n", src, strerror(errno)); return 1; }
    close(in);
    close(out);
    return 0;
}
