// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// wc [-c] [FILE...] -- lines, words and bytes. With -c it also prints an
// FNV-1a checksum, handy for proving a big file read back intact.

#include "kunix.h"

static int want_hash = 0;
static unsigned long tl, tw, tb;

static int count(int fd, const char* name) {
    char buf[4096];
    unsigned long lines = 0, words = 0, bytes = 0;
    unsigned long hash = 0xcbf29ce484222325UL;
    int in_word = 0;
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            unsigned char c = (unsigned char)buf[i];
            hash = (hash ^ c) * 0x100000001b3UL;
            if (c == '\n') lines++;
            if (isspace(c)) in_word = 0;
            else if (!in_word) { in_word = 1; words++; }
        }
        bytes += (unsigned long)n;
    }
    if (n < 0) { dprintf(STDERR_FILENO, "wc: %s: %s\n", name, strerror(errno)); return 1; }
    printf("%7lu %7lu %8lu", lines, words, bytes);
    if (want_hash) printf(" fnv1a:%016lx", hash);
    printf(" %s\n", name);
    tl += lines; tw += words; tb += bytes;
    return 0;
}

int main(int argc, char** argv) {
    int i = 1;
    if (i < argc && strcmp(argv[i], "-c") == 0) { want_hash = 1; i++; }
    if (i == argc) return count(STDIN_FILENO, "");
    int rc = 0, files = argc - i;
    for (; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) { dprintf(STDERR_FILENO, "wc: %s: %s\n", argv[i], strerror(errno)); rc = 1; continue; }
        rc |= count(fd, argv[i]);
        close(fd);
    }
    if (files > 1) printf("%7lu %7lu %8lu total\n", tl, tw, tb);
    return rc;
}
