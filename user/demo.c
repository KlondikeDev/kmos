// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// A ring 3 Kunix program written in C. Exercises every syscall the kernel
// currently offers -- write, getcpl, read and readfile -- plus the parts of
// the ELF loader a flat blob could never use: initialised globals (.data),
// zeroed globals (.bss), and constants in their own read-only segment.

#include "kunix.h"

static long counter = 41;            // .data: must arrive holding 41
static char contents[2048];          // .bss: must arrive zeroed

int main(void) {
    kputs("\nKunix C program, loaded from disk as an ELF.\n");

    long cpl = sys_getcpl();
    if (cpl != 3) {
        kputs("unexpected privilege level -- not ring 3?\n");
        return 1;
    }
    kputs("confirmed running at CPL 3.\n");

    counter++;
    if (counter != 42 || contents[0] != 0 || contents[sizeof(contents) - 1] != 0) {
        kputs(".data/.bss were not set up correctly\n");
        return 2;
    }
    kputs(".data and .bss look right.\n\n");

    // Read a line from the keyboard into a stack buffer and echo it back.
    char line[128];
    kputs("say something: ");
    long n = sys_read(KUNIX_STDIN, line, sizeof(line) - 1);
    if (n < 0) {
        kputs("read failed\n");
        return 1;
    }
    line[n] = '\0';
    kputs("you said: ");
    kputs(line);
    kputs("\n\n");

    // Pull a file off the FAT32 disk, this time into a global buffer.
    long len = sys_readfile("README.TXT", contents, sizeof(contents) - 1);
    if (len < 0) {
        kputs("README.TXT: not found\n");
    } else {
        contents[len] = '\0';
        kputs("README.TXT contains:\n");
        kputs(contents);
        kputs("\n");
    }

    return 0;
}
