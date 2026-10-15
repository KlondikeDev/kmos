// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// uname [-a] -- system name, or everything with -a.

#include "kunix.h"

int main(int argc, char** argv) {
    struct utsname u;
    if (uname(&u) < 0) { perror("uname"); return 1; }
    if (argc > 1 && strcmp(argv[1], "-a") == 0) {
        printf("%s %s %s %s\n", u.sysname, u.release, u.version, u.machine);
    } else {
        printf("%s\n", u.sysname);
    }
    return 0;
}
