// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// ps -- list processes.

#include "kunix.h"

int main(void) {
    static const char* states[] = { "unused", "embryo", "ready", "running", "blocked", "zombie" };
    procinfo_t p;
    printf("  PID  PPID  PGID  STATE    NAME\n");
    for (unsigned long i = 0; procinfo(i, &p) == 1; i++) {
        const char* st = p.state >= 0 && p.state <= 5 ? states[p.state] : "?";
        printf("%5d %5d %5d  %-8s %s\n", p.pid, p.ppid, p.pgid, st, p.name);
    }
    return 0;
}
