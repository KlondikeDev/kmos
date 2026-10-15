// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// CMOS real-time clock: read once at boot, then advanced by the PIT.
//
// The RTC keeps calendar time in a handful of CMOS registers, usually in
// BCD and sometimes in 12-hour form (status register B says which). It is
// read twice until two reads agree, since an update can land mid-read.

#include <types.h>
#include <rtc.h>
#include <io.h>
#include <pit.h>
#include <klog.h>

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static u64 boot_epoch;               // seconds since 1970 at boot
static u32 boot_ticks;               // PIT ticks when boot_epoch was read

static u8 cmos(u8 reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static bool updating(void) {
    return cmos(0x0A) & 0x80;
}

typedef struct { u8 s, m, h, day, mon, yr, cent; } raw_time_t;

static void read_raw(raw_time_t* t) {
    while (updating()) { }
    t->s = cmos(0x00);
    t->m = cmos(0x02);
    t->h = cmos(0x04);
    t->day = cmos(0x07);
    t->mon = cmos(0x08);
    t->yr = cmos(0x09);
    t->cent = cmos(0x32);            // the ACPI century register, if present
}

static u8 bcd(u8 v) {
    return (u8)((v & 0x0F) + (v >> 4) * 10);
}

// Days from 1970-01-01 to y-m-d (proleptic Gregorian), after Howard Hinnant.
static i64 days_from_civil(i64 y, u32 m, u32 d) {
    y -= m <= 2;
    i64 era = (y >= 0 ? y : y - 399) / 400;
    u32 yoe = (u32)(y - era * 400);
    u32 doy = (153 * (m + (m > 2 ? (u32)-3 : 9)) + 2) / 5 + d - 1;
    u32 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (i64)doe - 719468;
}

void rtc_init(void) {
    raw_time_t a, b;
    read_raw(&b);
    do {
        a = b;
        read_raw(&b);
    } while (a.s != b.s || a.m != b.m || a.h != b.h || a.day != b.day ||
             a.mon != b.mon || a.yr != b.yr);

    u8 regb = cmos(0x0B);
    bool binary = regb & 0x04;
    bool h24 = regb & 0x02;
    bool pm = b.h & 0x80;
    b.h &= 0x7F;
    if (!binary) {
        b.s = bcd(b.s); b.m = bcd(b.m); b.h = bcd(b.h);
        b.day = bcd(b.day); b.mon = bcd(b.mon); b.yr = bcd(b.yr);
        b.cent = bcd(b.cent);
    }
    if (!h24) {
        if (b.h == 12) b.h = 0;
        if (pm) b.h += 12;
    }
    u32 year = (b.cent >= 19 && b.cent <= 30) ? b.cent * 100u + b.yr : 2000u + b.yr;

    boot_epoch = (u64)(days_from_civil(year, b.mon, b.day) * 86400 +
                       b.h * 3600 + b.m * 60 + b.s);
    boot_ticks = ticks;
    klog(LOG_OKAY, "RTC: %u-%02u-%02u %02u:%02u:%02u UTC\n",
         year, b.mon, b.day, b.h, b.m, b.s);
}

u64 rtc_now(void) {
    return boot_epoch + (u64)(ticks - boot_ticks) / 250;     // PIT at 250 Hz
}

void rtc_to_date(u64 epoch, rtc_date_t* out) {
    i64 days = (i64)(epoch / 86400);
    u32 secs = (u32)(epoch % 86400);
    out->hour = secs / 3600;
    out->min = (secs / 60) % 60;
    out->sec = secs % 60;

    // Inverse of days_from_civil.
    days += 719468;
    i64 era = (days >= 0 ? days : days - 146096) / 146097;
    u32 doe = (u32)(days - era * 146097);
    u32 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    i64 y = (i64)yoe + era * 400;
    u32 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    u32 mp = (5 * doy + 2) / 153;
    out->day = doy - (153 * mp + 2) / 5 + 1;
    out->month = mp < 10 ? mp + 3 : mp - 9;
    out->year = (u32)(y + (out->month <= 2));
}
