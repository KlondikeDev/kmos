// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// rm [-r] PATH... -- remove files; -r also removes directories, recursively.

#include "kunix.h"

static int recursive = 0;

static int remove_path(const char* path) {
    struct stat st;
    if (stat(path, &st) < 0) {
        dprintf(STDERR_FILENO, "rm: %s: %s\n", path, strerror(errno));
        return 1;
    }
    if (!S_ISDIR(st.st_mode)) {
        if (unlink(path) < 0) { dprintf(STDERR_FILENO, "rm: %s: %s\n", path, strerror(errno)); return 1; }
        return 0;
    }
    if (!recursive) { dprintf(STDERR_FILENO, "rm: %s: is a directory\n", path); return 1; }

    // Removing entries shifts the listing, so always take entry 0 afresh.
    for (;;) {
        int fd = open(path, O_RDONLY | O_DIRECTORY);
        if (fd < 0) break;
        struct kdirent e;
        int r = readdir_fd(fd, &e);
        close(fd);
        if (r != 1) break;
        char child[256];
        snprintf(child, sizeof(child), "%s/%s", path, e.d_name);
        if (remove_path(child)) return 1;
    }
    if (rmdir(path) < 0) { dprintf(STDERR_FILENO, "rm: %s: %s\n", path, strerror(errno)); return 1; }
    return 0;
}

int main(int argc, char** argv) {
    int i = 1;
    if (i < argc && (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "-rf") == 0)) { recursive = 1; i++; }
    if (i == argc) { dprintf(STDERR_FILENO, "usage: rm [-r] PATH...\n"); return 2; }
    int rc = 0;
    for (; i < argc; i++) rc |= remove_path(argv[i]);
    return rc;
}
