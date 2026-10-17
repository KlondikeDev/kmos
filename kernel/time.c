// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <types.h>
#include <ktime.h>
#include <pit.h>
#include <rtc.h>
#include <io.h>

#define TICK_NS 4000000ULL           // PIT at 250 Hz

static u64 tsc_at_tick;              // TSC when the latest tick arrived
static u64 tsc_per_tick;             // measured from the last two ticks
static u64 last_ns;                  // the latest time handed out

static u64 rdtsc(void) {
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (u64)hi << 32 | lo;
}

void time_on_tick(void) {
    u64 now = rdtsc();
    if (tsc_at_tick && now > tsc_at_tick) {
        u64 d = now - tsc_at_tick;
        // Smooth it a little: a late interrupt should not skew the rate.
        tsc_per_tick = tsc_per_tick ? (tsc_per_tick * 7 + d) / 8 : d;
    }
    tsc_at_tick = now;
}

u64 time_boot_ns(void) {
    u64 flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    u64 ns = (u64)ticks * TICK_NS;
    if (tsc_per_tick) {
        u64 since = rdtsc() - tsc_at_tick;
        u64 frac = since < tsc_per_tick ? since * TICK_NS / tsc_per_tick : TICK_NS - 1;
        ns += frac;
    }
    if (ns <= last_ns) ns = last_ns + 1;
    last_ns = ns;
    __asm__ volatile("push %0; popfq" :: "r"(flags) : "memory", "cc");
    return ns;
}

u64 time_real_ns(void) {
    return rtc_epoch_of_tick0_ms() * 1000000ULL + time_boot_ns();
}

u64 time_counter_ns(void) {
    static u64 rate, origin, origin_ns;
    u64 flags = irq_save();
    if (!rate && tsc_per_tick) {
        rate = tsc_per_tick;
        origin = rdtsc();
        origin_ns = (u64)ticks * TICK_NS;
    }
    u64 now;
    if (rate) {
        u64 delta = rdtsc() - origin;
        now = origin_ns + delta / rate * TICK_NS + delta % rate * TICK_NS / rate;
    } else now = (u64)ticks * TICK_NS;
    irq_restore(flags);
    return now;
}
