// This file has been generated in whole or in part by a Large Language Model
#pragma once
#include <types.h>
// Host tests replace only privileged interrupt and UART access.
static u32 uart_writes;
static u8 uart_bytes[2];
static u32 uart_status_reads;
static inline u64 irq_save(void) { return 0; }
static inline void irq_restore(u64 flags) { (void)flags; }
static inline u8 inb(u16 port) {
    if (port == 0x3fd) return 0x20 | (++uart_status_reads >= 3 ? 1 : 0);
    if (port == 0x3f8) return uart_status_reads >= 3 ? 0xae : 0;
    return 0;
}
static inline void outb(u16 port, u8 value) {
    (void)port;
    uart_bytes[uart_writes++ % 2] = value;
}
