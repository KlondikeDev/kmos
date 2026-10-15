// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <types.h>
#include <gdt.h>
#include <klog.h>
#include <memset.h>

// Limine leaves us running on its own GDT, which has no ring 3 segments and
// no TSS. Neither can be bolted on afterwards, so we build a replacement.

#define GDT_ENTRIES 11   // 9 descriptors + the TSS, which is 16 bytes wide

// 64-bit TSS. Long mode ignores most of it; we only care about rsp0, the
// stack the CPU loads when a trap raises us out of ring 3.
typedef struct PACKED {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} tss_t;

typedef struct PACKED {
    uint16_t limit;
    uint64_t base;
} gdt_ptr_t;

static uint64_t gdt[GDT_ENTRIES];
static gdt_ptr_t gdtp;
static tss_t tss;

// Stack the CPU switches to on every ring 3 -> ring 0 transition. It has to
// live in kernel memory that ring 3 can never map, so .bss it is.
#define KERNEL_STACK_SIZE (16 * KB)
static uint8_t kernel_stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));

// Descriptors are mostly constant in long mode: the base and limit fields are
// ignored for code and data, so only the access and flag bytes carry meaning.
#define GDT_KCODE 0x00AF9A000000FFFFULL   // present, ring 0, exec, long mode
#define GDT_KDATA 0x00CF92000000FFFFULL   // present, ring 0, read/write
#define GDT_UDATA 0x00CFF2000000FFFFULL   // present, ring 3, read/write
#define GDT_UCODE 0x00AFFA000000FFFFULL   // present, ring 3, exec, long mode

static void gdt_set_tss(int idx, uint64_t base, uint32_t limit) {
    // A system descriptor is twice the width of a normal one; type 0x9 is
    // "64-bit TSS (available)", so the access byte comes out as 0x89.
    gdt[idx] = (limit & 0xFFFFULL)
             | ((base & 0xFFFFFFULL) << 16)
             | (0x89ULL << 40)
             | (((uint64_t)(limit >> 16) & 0xFULL) << 48)
             | (((base >> 24) & 0xFFULL) << 56);
    gdt[idx + 1] = (base >> 32) & 0xFFFFFFFFULL;
}

void tss_set_rsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}

uint64_t tss_get_rsp0(void) {
    return tss.rsp0;
}

void gdt_init(void) {
    memset(gdt, 0, sizeof(gdt));
    memset(&tss, 0, sizeof(tss));

    gdt[GDT_KERNEL_CS / 8] = GDT_KCODE;
    gdt[GDT_KERNEL_DS / 8] = GDT_KDATA;
    gdt[GDT_USER_DS / 8]   = GDT_UDATA;
    gdt[GDT_USER_CS / 8]   = GDT_UCODE;

    // No I/O permission bitmap: pointing iomap_base past the segment limit
    // tells the CPU that every port access from ring 3 is denied.
    tss.iomap_base = sizeof(tss_t);
    tss.rsp0 = (uint64_t)kernel_stack + KERNEL_STACK_SIZE;
    gdt_set_tss(GDT_TSS / 8, (uint64_t)&tss, sizeof(tss_t) - 1);

    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base = (uint64_t)gdt;

    // lgdt does not refresh the segment registers' hidden descriptor caches,
    // so CS is reloaded with a far return and the data selectors by hand.
    __asm__ volatile(
        "lgdt %0\n\t"
        "pushq %1\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "mov %2, %%eax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        :
        : "m"(gdtp), "i"((uint64_t)GDT_KERNEL_CS), "i"(GDT_KERNEL_DS)
        : "rax", "memory");

    __asm__ volatile("ltr %%ax" :: "a"((uint16_t)GDT_TSS));

    klog(LOG_OKAY, "GDT loaded with ring 3 segments, TSS rsp0=0x%lx\n", tss.rsp0);
}
