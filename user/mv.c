// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// mv SRC DST -- rename, or across filesystems copy and then unlink.
// DST may be an existing directory to move SRC into.

#include "kunix.h"

int main(int argc, char** argv) {
    if (argc != 3) { dprintf(STDERR_FILENO, "usage: mv SRC DST\n"); return 2; }
    char dst[256];
    struct stat st;
    if (stat(argv[2], &st) == 0 && S_ISDIR(st.st_mode)) {
        const char* base = strrchr(argv[1], '/');
        snprintf(dst, sizeof(dst), "%s/%s", argv[2], base ? base + 1 : argv[1]);
    } else {
        strncpy(dst, argv[2], sizeof(dst) - 1);
        dst[sizeof(dst) - 1] = '\0';
    }

    if (rename(argv[1], dst) == 0) return 0;
    if (errno != EXDEV) {
        dprintf(STDERR_FILENO, "mv: %s -> %s: %s\n", argv[1], dst, strerror(errno));
        return 1;
    }

    // Different filesystems: copy with cp, then remove the original.
    char* cp_argv[] = { "cp", argv[1], dst, NULL };
    pid_t pid = fork();
    if (pid == 0) { execvp("cp", cp_argv); _exit(127); }
    int status;
    if (pid < 0 || waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status)) {
        dprintf(STDERR_FILENO, "mv: copy failed, %s left in place\n", argv[1]);
        return 1;
    }
    if (unlink(argv[1]) < 0) { dprintf(STDERR_FILENO, "mv: %s: %s\n", argv[1], strerror(errno)); return 1; }
    return 0;
}
