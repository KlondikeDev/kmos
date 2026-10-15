// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>

// Selector layout. The kernel selectors deliberately keep the same offsets
// Limine hands us (0x28 code / 0x30 data) so the IDT gates and every segment
// register already loaded stay valid across the switch to our own GDT.
#define GDT_KERNEL_CS 0x28
#define GDT_KERNEL_DS 0x30
#define GDT_USER_DS   0x38
#define GDT_USER_CS   0x40
#define GDT_TSS       0x48

// The selectors as ring 3 sees them, RPL included.
#define GDT_USER_DS_RPL3 (GDT_USER_DS | 3)
#define GDT_USER_CS_RPL3 (GDT_USER_CS | 3)

void gdt_init(void);

// Point the TSS at the stack the CPU switches to on a ring 3 -> ring 0 trap.
void tss_set_rsp0(uint64_t rsp0);
uint64_t tss_get_rsp0(void);
