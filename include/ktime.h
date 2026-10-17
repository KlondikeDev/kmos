// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>

// High-resolution time. The PIT gives 4 ms steps; within a step the CPU's
// timestamp counter is interpolated, calibrated against the PIT tick by
// tick. Every call returns a later time than the one before, so two
// readings never tie (libc builds unique temporary names from them).

void time_on_tick(void);            // from the timer IRQ, once per tick
u64 time_boot_ns(void);             // nanoseconds since boot
u64 time_counter_ns(void);          // TSC deadline clock; progresses with IRQs off
u64 time_real_ns(void);             // nanoseconds since 1970 (UTC)
