// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>

typedef struct {
    u32 year, month, day;
    u32 hour, min, sec;
} rtc_date_t;

// Read the CMOS clock. Call after the PIT is running.
void rtc_init(void);

// Seconds since 1970-01-01 00:00:00 UTC.
u64 rtc_now(void);
// The same, in milliseconds (4 ms resolution: the PIT tick).
u64 rtc_now_ms(void);

void rtc_to_date(u64 epoch, rtc_date_t* out);
u64 rtc_from_date(const rtc_date_t* d);       // the inverse
