// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// free -- physical memory and kernel heap usage.

#include "kunix.h"

int main(void) {
    struct meminfo m;
    if (meminfo(&m) < 0) { perror("free"); return 1; }
    unsigned long total = m.total_pages * 4, freek = m.free_pages * 4;
    printf("            total        used        free\n");
    printf("Mem:   %8lu KiB %8lu KiB %8lu KiB\n", total, total - freek, freek);
    printf("Kheap: %8lu KiB %8lu KiB %8lu KiB\n", (m.heap_used + m.heap_free) / 1024,
           m.heap_used / 1024, m.heap_free / 1024);
    return 0;
}
