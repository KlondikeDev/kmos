// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// mv SRC DST -- there is no rename() yet, so this copies, then unlinks.

#include "kunix.h"

int main(int argc, char** argv) {
    if (argc != 3) { dprintf(STDERR_FILENO, "usage: mv SRC DST\n"); return 2; }
    char* cp_argv[] = { "cp", argv[1], argv[2], NULL };
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
