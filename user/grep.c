// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// grep [-v] [-c] [-i] PATTERN [FILE...] -- lines containing a fixed string.

#include "kunix.h"

static int invert = 0, count_only = 0, nocase = 0;

static int contains(const char* line, const char* pat) {
    size_t n = strlen(pat);
    for (; *line; line++) {
        size_t k = 0;
        for (; k < n && line[k]; k++) {
            char a = line[k], b = pat[k];
            if (nocase) {
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
            }
            if (a != b) break;
        }
        if (k == n) return 1;
    }
    return n == 0;
}

static long scan(int fd, const char* pat, const char* label) {
    static char line[4096];
    size_t len = 0;
    long matches = 0;
    char buf[4096];
    ssize_t n;
    for (;;) {
        n = read(fd, buf, sizeof(buf));
        for (ssize_t i = 0; i < n || (n <= 0 && len); i++) {
            int eol = n <= 0 || buf[i] == '\n';
            if (!eol && len < sizeof(line) - 1) { line[len++] = buf[i]; continue; }
            line[len] = '\0';
            if (contains(line, pat) != invert) {
                matches++;
                if (!count_only) {
                    if (label) printf("%s:", label);
                    printf("%s\n", line);
                }
            }
            len = 0;
            if (n <= 0) break;
        }
        if (n <= 0) break;
    }
    return matches;
}

int main(int argc, char** argv) {
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        for (const char* f = argv[i] + 1; *f; f++) {
            if (*f == 'v') invert = 1;
            else if (*f == 'c') count_only = 1;
            else if (*f == 'i') nocase = 1;
        }
    }
    if (i == argc) { dprintf(STDERR_FILENO, "usage: grep [-vci] PATTERN [FILE...]\n"); return 2; }
    const char* pat = argv[i++];
    long total = 0;
    if (i == argc) {
        total = scan(STDIN_FILENO, pat, NULL);
    } else {
        int many = argc - i > 1;
        for (; i < argc; i++) {
            int fd = open(argv[i], O_RDONLY);
            if (fd < 0) { dprintf(STDERR_FILENO, "grep: %s: %s\n", argv[i], strerror(errno)); continue; }
            total += scan(fd, pat, many ? argv[i] : NULL);
            close(fd);
        }
    }
    if (count_only) printf("%ld\n", total);
    return total ? 0 : 1;
}
