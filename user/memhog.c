// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// memhog [N] -- fork N children that each grab and touch as much memory as
// they can, to drive the machine out of physical memory. The kernel must
// refuse allocations gracefully (fork/sbrk failing, stack growth killing the
// one process) and stay up. Reports how far each child got.

#include "kunix.h"

int main(int argc, char** argv) {
    int n = argc > 1 ? atoi(argv[1]) : 24;
    struct meminfo before;
    meminfo(&before);
    printf("memhog: %d children, %lu KiB free\n", n, before.free_pages * 4);

    int forked = 0;
    for (int i = 0; i < n; i++) {
        pid_t pid = fork();
        if (pid < 0) { printf("memhog: fork %d failed: %s\n", i, strerror(errno)); break; }
        if (pid == 0) {
            unsigned long got = 0;
            for (;;) {
                char* p = malloc(256 * 1024);
                if (!p) break;
                for (int k = 0; k < 256 * 1024; k += 4096) p[k] = 1;
                got += 256;
            }
            msleep(1500);                    // hold it while the others try
            _exit((int)(got / 1024));        // MiB obtained
        }
        forked++;
    }
    int total = 0, status;
    pid_t pid;
    while ((pid = wait(&status)) > 0) {
        if (WIFEXITED(status)) total += WEXITSTATUS(status);
        else printf("memhog: child %d died: %s\n", pid, signame(WTERMSIG(status)));
    }
    struct meminfo after;
    meminfo(&after);
    printf("memhog: %d children got %d MiB in all; %lu KiB free before, %lu after\n",
           forked, total, before.free_pages * 4, after.free_pages * 4);
    return after.free_pages == before.free_pages ? 0 : 1;
}
