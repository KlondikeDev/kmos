// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// kill [-SIG] PID... -- send a signal (default TERM). A negative PID names a
// process group. SIG may be a number or a name: kill -INT 5, kill -9 5.

#include "kunix.h"

static int parse_sig(const char* s) {
    static const struct { const char* n; int v; } names[] = {
        {"HUP",SIGHUP},{"INT",SIGINT},{"QUIT",SIGQUIT},{"KILL",SIGKILL},{"SEGV",SIGSEGV},
        {"PIPE",SIGPIPE},{"TERM",SIGTERM},{"USR1",SIGUSR1},{"USR2",SIGUSR2},{"CHLD",SIGCHLD},
    };
    if (isdigit(*s)) return atoi(s);
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strcmp(s, names[i].n) == 0) return names[i].v;
    }
    return -1;
}

int main(int argc, char** argv) {
    int sig = SIGTERM, i = 1;
    if (i < argc && argv[i][0] == '-' && !isdigit(argv[i][1])) {
        sig = parse_sig(argv[i] + 1);
        i++;
    } else if (i < argc && argv[i][0] == '-' && i + 1 < argc) {
        sig = atoi(argv[i] + 1);
        i++;
    }
    if (sig < 0) { dprintf(STDERR_FILENO, "kill: unknown signal\n"); return 2; }
    if (i == argc) { dprintf(STDERR_FILENO, "usage: kill [-SIG] PID...\n"); return 2; }
    int rc = 0;
    for (; i < argc; i++) {
        if (kill(atoi(argv[i]), sig) < 0) {
            dprintf(STDERR_FILENO, "kill: %s: %s\n", argv[i], strerror(errno));
            rc = 1;
        }
    }
    return rc;
}
