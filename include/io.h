// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include "types.h"

void outb(u16 port, u8 val);

u8 inb(u16 port);

void outw(u16 port, u16 val);

u16 inw(u16 port);

void outl(uint16_t port, uint32_t val);

uint32_t inl(uint16_t port);


// Disable interrupts and return the previous RFLAGS, so the caller's
// interrupt state can be restored exactly (handles nesting / panic / IRQ context).
static inline uint64_t irq_save(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags) {
    __asm__ volatile("push %0; popfq" :: "r"(flags) : "memory", "cc");
}