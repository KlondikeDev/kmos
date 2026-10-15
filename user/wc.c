// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// wc FILE -- count lines and bytes, show the first and last line, and print
// an FNV-1a checksum of the contents. Compare the checksum with the host's
// copy to prove a large file read back byte-for-byte intact.

#include "kunix.h"

#define WC_MAX (3600 * 1024)         // a bit under the per-process page cap

static void put_line(const char* s, const char* end) {
    const char* e = s;
    while (e < end && *e != '\n') e++;
    sys_write(KUNIX_STDOUT, s, (u64)(e - s));
    kputs("\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        kputs("usage: wc FILE\n");
        return 1;
    }
    char* buf = kalloc(WC_MAX);
    if (!buf) {
        kputs("wc: out of memory\n");
        return 1;
    }
    long n = sys_readfile(argv[1], buf, WC_MAX);
    if (n < 0) {
        kputs("wc: ");
        kputs(argv[1]);
        kputs(": no such file\n");
        return 1;
    }

    long lines = 0;
    u64 hash = 0xcbf29ce484222325UL;                 // FNV-1a, 64-bit
    for (long i = 0; i < n; i++) {
        if (buf[i] == '\n') lines++;
        hash = (hash ^ (unsigned char)buf[i]) * 0x100000001b3UL;
    }
    kputi(lines);
    kputs(" lines, ");
    kputi(n);
    kputs(" bytes, fnv1a ");
    char hex[17];
    for (int i = 15; i >= 0; i--) { hex[i] = "0123456789abcdef"[hash & 15]; hash >>= 4; }
    hex[16] = '\0';
    kputs(hex);
    kputs("\n");
    if (n == 0) return 0;

    kputs("first: ");
    put_line(buf, buf + n);
    long last = n - 1;
    if (buf[last] == '\n') last--;
    while (last > 0 && buf[last - 1] != '\n') last--;
    kputs("last:  ");
    put_line(buf + last, buf + n);
    return 0;
}
