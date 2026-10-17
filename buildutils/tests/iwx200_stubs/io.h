// This file has been generated in whole or in part by a Large Language Model
#pragma once
#include <types.h>
void outl(u16 port, u32 value);
void outw(u16 port, u16 value);
static inline u64 irq_save(void) { return 0; }
static inline void irq_restore(u64 flags) { (void)flags; }
