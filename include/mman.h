// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>

// Memory mappings: mmap, munmap, mprotect, and the page faults that fill
// mappings in lazily. Flag values are Linux's (the BSDs agree on all but
// MAP_ANONYMOUS), so Prime Meridian can pass them straight through.

#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

#define MAP_SHARED          0x01
#define MAP_PRIVATE         0x02
#define MAP_SHARED_VALIDATE 0x03
#define MAP_TYPE            0x0F
#define MAP_FIXED           0x10
#define MAP_ANONYMOUS       0x20
#define MAP_GROWSDOWN       0x0100      // accepted, ignored
#define MAP_DENYWRITE       0x0800      // accepted, ignored
#define MAP_EXECUTABLE      0x1000      // accepted, ignored
#define MAP_NORESERVE       0x4000      // accepted, ignored
#define MAP_POPULATE        0x8000      // fault every page in now
#define MAP_NONBLOCK        0x10000     // accepted, ignored
#define MAP_STACK           0x20000     // accepted, ignored
#define MAP_FIXED_NOREPLACE 0x100000    // MAP_FIXED, but EEXIST over a mapping

// A virtual memory area: an mmap region or part of a demand-paged image.
// The brk heap and stack still keep their own bookkeeping in process_t.
#define VMA_SHARED 0x1                  // MAP_SHARED: fork shares, not copies

#define MAX_VMAS 256

struct vm_backing;
struct process;

typedef struct {
    u64 start, end;                     // page-aligned, end exclusive
    u32 prot;                           // PROT_*
    u32 flags;                          // VMA_*
    struct vm_backing* backing;          // pins the file until the last mapping
    u64 offset;                         // file offset corresponding to start
} vma_t;

// The system calls. Results are >= 0 (an address, for mmap) or -errno.
i64 mm_mmap(u64 addr, u64 len, u64 prot, u64 flags, i64 fd, u64 off);
i64 mm_munmap(u64 addr, u64 len);
i64 mm_mprotect(u64 addr, u64 len, u64 prot);

// Resolve a fault at addr in the current process: copy a copy-on-write
// page, fill a mapped-but-untouched page, or grow the stack. True if the
// access can now be retried.
bool mm_fault(u64 addr, bool write);
void mm_release(struct process* p);
void mm_release_backings(vma_t* areas, u32 count);
void mm_clone_backings(struct process* child);

// Does [start, end) overlap any VMA of the current process?
bool mm_overlaps(u64 start, u64 end);
