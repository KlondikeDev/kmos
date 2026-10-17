// This file has been generated in whole or in part by a Large Language Model
#pragma once
#include <types.h>
static inline uint64_t irq_save(void) { return 0; }
static inline void irq_restore(uint64_t f) { (void)f; }
void outl(uint16_t port, uint32_t value);
void outw(u16 port, u16 value);
u16 inw(u16 port);
