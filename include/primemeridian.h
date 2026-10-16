// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

// Prime Meridian: the Linux personality.
//
// A Linux x86_64 program enters the kernel with the `syscall` instruction,
// so that instruction *is* the personality gate: it lands in Prime
// Meridian, which speaks Linux's numbers, structures and signal frames on
// top of the native kernel. Kunix's own programs use `int 0x80` and never
// come through here. (FreeBSD programs use `syscall` too; Inferno will tell
// them apart by their ELF brand when it arrives.)
//
// The code lives in compat/primemeridian/.

#include <types.h>
#include <idt.h>
#include <proc.h>

// Program the syscall MSRs. Call once, after the GDT is loaded.
void pm_init(void);

// The kernel stack `syscall` should switch to: the current process's.
void pm_set_kernel_stack(u64 top);

// Called by the signal core for a handler installed with rt_sigaction:
// build a Linux rt_sigframe and point regs at the handler. False if the
// stack cannot take it (the caller then kills the process).
bool pm_signal_setup(process_t* p, registers_t* regs, i32 sig, u64 handler, bool from_syscall);
