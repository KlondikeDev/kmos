// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// mmap, munmap and mprotect.
//
// A process's mappings are a sorted array of VMAs (process_t.vmas). Pages
// inside a VMA are only backed when first touched: mm_fault, called from
// the page fault handler and from user_range_ok when a syscall is handed a
// buffer, maps a zeroed frame then. The exceptions are filled in at mmap
// time -- file mappings (read in whole; there is no page cache to fault
// from yet), MAP_SHARED anonymous memory (so that fork finds the frames to
// share), and MAP_POPULATE.
//
// Page protections map onto PTEs as:
//   PROT_NONE          present but not user-accessible, so any touch faults
//   PROT_READ          user, read-only
//   PROT_WRITE         user, writable -- or read-only + PTE_COW while the
//                      frame is shared with another process after fork
//   no PROT_EXEC       NX (when the CPU supports it)

#include <types.h>
#include <mman.h>
#include <proc.h>
#include <usermode.h>
#include <memory.h>
#include <memset.h>
#include <file.h>
#include <vfs.h>
#include <elf.h>
#include <errno.h>
#include <klog.h>
#include <fb.h>

#define PAGE      USER_PAGE_SIZE
#define PAGE_MASK (~(PAGE - 1))
#define PTE_ADDR  0x000FFFFFFFFFF000ULL

// Note: bool is a u8 here (types.h), so a mask wider than bit 7 must be
// compared (!= 0) before it is stored in one, or it truncates to false.

static u32 prot_to_pf(u32 prot) {
    u32 pf = 0;
    if (prot & PROT_READ)  pf |= PF_R;
    if (prot & PROT_WRITE) pf |= PF_W;
    if (prot & PROT_EXEC)  pf |= PF_X;
    return pf;
}

static bool nx_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        uint32_t lo, hi;
        __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080));
        cached = (lo >> 11) & 1;
    }
    return cached;
}

// The PTE flag word for a present page with these rights.
static u64 pte_flags(u64 frame, u32 prot, bool shared) {
    u64 f = PTE_PRESENT | fb_cache_flags(frame);
    if (prot != PROT_NONE) f |= PTE_USER;
    if (prot & PROT_WRITE) {
        // A private frame someone else still holds must be copied on write.
        if (shared || pmm_refs(frame) <= 1) f |= PTE_WRITE;
        else f |= PTE_COW;
    }
    if (!(prot & PROT_EXEC) && nx_enabled()) f |= PTE_NX;
    if (shared) f |= PTE_SHARED;
    return f;
}

// ---------------------------------------------------------------------------
// The VMA array
// ---------------------------------------------------------------------------

// Open a hole at index i (the array has room for one more).
static void vma_open_slot(process_t* p, u32 i) {
    for (u32 j = p->nvmas; j > i; j--) p->vmas[j] = p->vmas[j - 1];
    p->nvmas++;
}

static vma_t* vma_find(process_t* p, u64 addr) {
    for (u32 i = 0; i < p->nvmas; i++) {
        if (addr < p->vmas[i].start) return NULL;          // sorted
        if (addr < p->vmas[i].end) return &p->vmas[i];
    }
    return NULL;
}

bool mm_overlaps(u64 start, u64 end) {
    process_t* p = proc_current();
    for (u32 i = 0; i < p->nvmas; i++) {
        if (p->vmas[i].start < end && start < p->vmas[i].end) return true;
    }
    return false;
}

// Make `at` a boundary between VMAs, splitting the one that spans it.
static bool vma_split_at(process_t* p, u64 at) {
    for (u32 i = 0; i < p->nvmas; i++) {
        vma_t* v = &p->vmas[i];
        if (at <= v->start) return true;
        if (at < v->end) {
            if (p->nvmas == MAX_VMAS) return false;
            vma_open_slot(p, i);
            p->vmas[i].end = at;
            p->vmas[i + 1].start = at;
            return true;
        }
    }
    return true;
}

// Drop every VMA inside [start, end). Boundaries must already be split.
static void vma_remove(process_t* p, u64 start, u64 end) {
    u32 out = 0;
    for (u32 i = 0; i < p->nvmas; i++) {
        if (p->vmas[i].start >= start && p->vmas[i].end <= end) continue;
        p->vmas[out++] = p->vmas[i];
    }
    p->nvmas = out;
}

// Glue neighbours that touch and agree on everything.
static void vma_merge(process_t* p) {
    u32 out = 0;
    for (u32 i = 0; i < p->nvmas; i++) {
        if (out > 0) {
            vma_t* prev = &p->vmas[out - 1];
            if (prev->end == p->vmas[i].start && prev->prot == p->vmas[i].prot &&
                prev->flags == p->vmas[i].flags) {
                prev->end = p->vmas[i].end;
                continue;
            }
        }
        p->vmas[out++] = p->vmas[i];
    }
    p->nvmas = out;
}

// Add a VMA over a range that no VMA covers.
static bool vma_insert(process_t* p, u64 start, u64 end, u32 prot, u32 flags) {
    if (p->nvmas == MAX_VMAS) return false;
    u32 i = 0;
    while (i < p->nvmas && p->vmas[i].start < start) i++;
    vma_open_slot(p, i);
    p->vmas[i] = (vma_t){ .start = start, .end = end, .prot = prot, .flags = flags };
    vma_merge(p);
    return true;
}

// Top-down first fit in the mmap area. 0 if nothing is free.
static u64 find_gap(process_t* p, u64 len) {
    u64 hi = USER_MMAP_TOP;
    for (i32 i = (i32)p->nvmas - 1; ; i--) {
        u64 lo = USER_MMAP_BASE;
        if (i >= 0) {
            if (p->vmas[i].start >= hi) continue;          // above the window
            if (p->vmas[i].end > lo) lo = p->vmas[i].end;
        }
        if (hi > lo && hi - lo >= len) return hi - len;
        if (i < 0) return 0;
        hi = p->vmas[i].start;
        if (hi <= USER_MMAP_BASE) return 0;
    }
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------

// Free every page mapped in [start, end) -- whatever put it there.
static void unmap_pages(process_t* p, u64 start, u64 end) {
    for (u64 va = vmm_next_mapped(p->space, start, end); va < end;
         va = vmm_next_mapped(p->space, va + PAGE, end)) {
        u64 pte = vmm_get_pte(p->space, va);
        vmm_unmap_in(p->space, va);
        pmm_free(pte & PTE_ADDR);
        if (p->user_pages) p->user_pages--;
    }
}

// Back one page of a VMA with a zeroed frame.
static bool fill_page(process_t* p, u64 va, const vma_t* v) {
    if (!user_map(p, va, prot_to_pf(v->prot))) return false;
    u64 frame = vmm_get_pte(p->space, va) & PTE_ADDR;
    return vmm_map_in(p->space, va, frame, pte_flags(frame, v->prot, (v->flags & VMA_SHARED) != 0));
}

bool mm_fault(u64 addr, bool write) {
    process_t* p = proc_current();
    if (p->pid == 0 || addr >= USER_STACK_TOP) return false;
    u64 va = addr & PAGE_MASK;
    u64 pte = vmm_get_pte(p->space, va);

    if (pte & PTE_PRESENT) {
        // Present: the only fixable case is a write to a copy-on-write page.
        if (!write || !(pte & PTE_COW) || !(pte & PTE_USER)) return false;
        if (vmm_break_cow(p->space, va)) return true;
        klog(LOG_WARN, "mm: pid %u out of memory copying a page\n", (u32)p->pid);
        return false;
    }

    vma_t* v = vma_find(p, va);
    if (v) {
        if (v->prot == PROT_NONE) return false;
        if (write && !(v->prot & PROT_WRITE)) return false;
        return fill_page(p, va, v);
    }
    return user_grow_stack(addr);
}

// ---------------------------------------------------------------------------
// The system calls
// ---------------------------------------------------------------------------

i64 mm_mmap(u64 addr, u64 len, u64 prot, u64 flags, i64 fd, u64 off) {
    process_t* p = proc_current();
    u32 type = flags & MAP_TYPE;
    bool shared = type == MAP_SHARED || type == MAP_SHARED_VALIDATE;
    bool anon = (flags & MAP_ANONYMOUS) != 0;
    bool fixed = (flags & (MAP_FIXED | MAP_FIXED_NOREPLACE)) != 0;

    if (len == 0 || (off & (PAGE - 1)) || (prot & ~7ULL)) return -EINVAL;
    if (!shared && type != MAP_PRIVATE) return -EINVAL;
    if (len > USER_MMAP_TOP) return -ENOMEM;
    len = (len + PAGE - 1) & PAGE_MASK;

    // File mappings are copied in whole now. Writes cannot reach the file,
    // so a shared writable one is refused rather than silently diverging.
    file_t* f = NULL;
    i64 device = -1;                   // a device's physical memory, mapped as is
    if (!anon) {
        f = fd_get(p, (i32)fd);
        if (!f) return -EBADF;
        if (f->node && f->node->type == VN_CHR && f->node->mnt && f->node->mnt->ops->mmap) {
            if (!shared) return -EINVAL;
            if ((f->flags & O_ACCMODE) == O_WRONLY) return -EACCES;
            if ((prot & PROT_WRITE) && (f->flags & O_ACCMODE) == O_RDONLY) return -EACCES;
            device = f->node->mnt->ops->mmap(f->node, off, len);
            if (device < 0) return device;
            f = NULL;
        }
    }
    if (f) {
        if (!f->node || f->node->type != VN_REG) return -ENODEV;
        if ((f->flags & O_ACCMODE) == O_WRONLY) return -EACCES;
        if (shared && (prot & PROT_WRITE)) return -ENODEV;
        if (off + len < off) return -EINVAL;
        shared = false;
    }

    if (fixed) {
        if (addr & (PAGE - 1)) return -EINVAL;
        if (addr < USER_MMAP_MIN || addr + len < addr || addr + len > USER_MMAP_TOP) return -ENOMEM;
        if ((flags & MAP_FIXED_NOREPLACE) &&
            (mm_overlaps(addr, addr + len) || vmm_next_mapped(p->space, addr, addr + len) < addr + len)) {
            return -EEXIST;
        }
    } else {
        // A hint is honoured if that range is entirely free.
        addr &= PAGE_MASK;
        if (!addr || addr < USER_MMAP_MIN || addr + len < addr || addr + len > USER_MMAP_TOP ||
            mm_overlaps(addr, addr + len) ||
            vmm_next_mapped(p->space, addr, addr + len) < addr + len ||
            (addr < USER_MMAP_BASE && addr + len > p->brk_base)) {
            addr = find_gap(p, len);
            if (!addr) return -ENOMEM;
        }
    }

    u64 end = addr + len;
    bool eager = shared || f || device >= 0 || (flags & MAP_POPULATE) != 0;
    if (eager && p->user_pages + len / PAGE > USER_MAX_PAGES) return -ENOMEM;

    // MAP_FIXED replaces whatever was there.
    if (!vma_split_at(p, addr) || !vma_split_at(p, end)) return -ENOMEM;
    vma_remove(p, addr, end);
    unmap_pages(p, addr, end);
    if (!vma_insert(p, addr, end, (u32)prot, shared ? VMA_SHARED : 0)) return -ENOMEM;

    if (device >= 0) {
        // Device memory belongs to no one: the frame allocator never hands
        // it out and pmm_free leaves it alone, so unmapping, exit and fork
        // (which shares it, as MAP_SHARED) need nothing more.
        for (u64 va = addr; va < end; va += PAGE) {
            u64 frame = (u64)device + (va - addr);
            if (!vmm_map_in(p->space, va, frame, pte_flags(frame, (u32)prot, true))) goto oom;
            p->user_pages++;
        }
        return (i64)addr;
    }
    if (eager) {
        vma_t* v = vma_find(p, addr);
        u64 filled = 0;
        if (f) {
            filled = f->node->size > off ? f->node->size - off : 0;
            if (filled > len) filled = len;
            filled = (filled + PAGE - 1) & PAGE_MASK;
        }
        if (shared || (flags & MAP_POPULATE)) filled = len;
        for (u64 va = addr; va < addr + filled; va += PAGE) {
            if (!fill_page(p, va, v)) goto oom;
            if (f) {
                // Straight into the frame through the direct map; the page
                // may well be read-only to the program.
                u64 frame = vmm_get_pte(p->space, va) & PTE_ADDR;
                i64 n = vfs_read(f->node, off + (va - addr), (void*)(frame + vmm_hhdm()), PAGE);
                if (n < 0) {
                    mm_munmap(addr, len);
                    return n;
                }
                if (n < (i64)PAGE) break;                    // end of file: rest is zero
            }
        }
    }
    return (i64)addr;

oom:
    mm_munmap(addr, len);
    return -ENOMEM;
}

i64 mm_munmap(u64 addr, u64 len) {
    process_t* p = proc_current();
    if ((addr & (PAGE - 1)) || len == 0) return -EINVAL;
    len = (len + PAGE - 1) & PAGE_MASK;
    u64 end = addr + len;
    if (end < addr || end > USER_STACK_TOP) return -EINVAL;
    if (!vma_split_at(p, addr) || !vma_split_at(p, end)) return -ENOMEM;
    vma_remove(p, addr, end);
    unmap_pages(p, addr, end);
    return 0;
}

i64 mm_mprotect(u64 addr, u64 len, u64 prot) {
    process_t* p = proc_current();
    if ((addr & (PAGE - 1)) || (prot & ~7ULL)) return -EINVAL;
    if (len == 0) return 0;
    len = (len + PAGE - 1) & PAGE_MASK;
    u64 end = addr + len;
    if (end < addr || end > USER_STACK_TOP) return -ENOMEM;

    // Every page must be mapped: in a VMA, or present (image, heap, stack).
    for (u64 va = addr; va < end; ) {
        vma_t* v = vma_find(p, va);
        if (v) { va = v->end; continue; }
        if (!(vmm_get_pte(p->space, va) & PTE_PRESENT)) return -ENOMEM;
        va += PAGE;
    }

    if (!vma_split_at(p, addr) || !vma_split_at(p, end)) return -ENOMEM;
    for (u32 i = 0; i < p->nvmas; i++) {
        if (p->vmas[i].start >= addr && p->vmas[i].end <= end) p->vmas[i].prot = (u32)prot;
    }
    vma_merge(p);

    for (u64 va = vmm_next_mapped(p->space, addr, end); va < end;
         va = vmm_next_mapped(p->space, va + PAGE, end)) {
        u64 pte = vmm_get_pte(p->space, va);
        u64 frame = pte & PTE_ADDR;
        if (!vmm_map_in(p->space, va, frame, pte_flags(frame, (u32)prot, (pte & PTE_SHARED) != 0))) {
            return -ENOMEM;
        }
    }
    return 0;
}
