// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// demo -- a tour: identity, environment, a line from the keyboard, a file.

#include "kunix.h"

static long counter = 41;            // .data: must arrive holding 41
static char zeroed[2048];            // .bss: must arrive zeroed

int main(int argc, char** argv) {
    struct utsname u;
    uname(&u);
    printf("\n%s %s on %s, pid %d (parent %d), ring %ld\n",
           u.sysname, u.release, u.machine, getpid(), getppid(), getcpl());
    printf("argv:");
    for (int i = 0; i < argc; i++) printf(" [%s]", argv[i]);
    printf("\nPATH=%s\n", getenv("PATH") ? getenv("PATH") : "(unset)");

    counter++;
    if (counter != 42 || zeroed[0] || zeroed[sizeof(zeroed) - 1]) {
        puts(".data/.bss were not set up correctly");
        return 2;
    }

    printf("say something: ");
    char line[128];
    ssize_t n = read(STDIN_FILENO, line, sizeof(line) - 1);
    if (n < 0) { perror("read"); return 1; }
    line[n] = '\0';
    printf("you said: %s", line);
    return 0;
}
