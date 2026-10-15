// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// ls [-l] [-a] [PATH...] -- list directories (or name files).

#include "kunix.h"

static int long_fmt = 0;

static void show(const char* name, unsigned type, unsigned long size) {
    if (long_fmt) {
        char t = type == DT_DIR ? 'd' : type == DT_CHR ? 'c' : type == DT_FIFO ? 'p' : '-';
        printf("%c %10lu  %s%s\n", t, size, name, type == DT_DIR ? "/" : "");
    } else {
        printf("%s%s\n", name, type == DT_DIR ? "/" : "");
    }
}

static int list(const char* path, int show_header) {
    struct stat st;
    if (stat(path, &st) < 0) {
        dprintf(STDERR_FILENO, "ls: %s: %s\n", path, strerror(errno));
        return 1;
    }
    if (!S_ISDIR(st.st_mode)) {
        show(path, S_ISREG(st.st_mode) ? DT_REG : DT_CHR, st.st_size);
        return 0;
    }
    int fd = open(path, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        dprintf(STDERR_FILENO, "ls: %s: %s\n", path, strerror(errno));
        return 1;
    }
    if (show_header) printf("%s:\n", path);
    struct kdirent e;
    while (readdir_fd(fd, &e) == 1) show(e.d_name, e.d_type, e.d_size);
    close(fd);
    return 0;
}

int main(int argc, char** argv) {
    int first = 1;
    while (first < argc && argv[first][0] == '-') {
        for (const char* f = argv[first] + 1; *f; f++) {
            if (*f == 'l') long_fmt = 1;
            else if (*f != 'a') { dprintf(STDERR_FILENO, "ls: unknown option -%c\n", *f); return 2; }
        }
        first++;
    }
    if (first == argc) return list(".", 0);
    int rc = 0;
    for (int i = first; i < argc; i++) {
        rc |= list(argv[i], argc - first > 1);
        if (argc - first > 1 && i + 1 < argc) puts("");
    }
    return rc;
}
