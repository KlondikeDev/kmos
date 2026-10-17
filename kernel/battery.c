// This file has been generated in whole or in part by a Large Language Model
// Native power telemetry. Firmware work belongs to the kernel task, never a
// read syscall or timer interrupt. Readers receive the last complete sample.
#include <battery.h>
#ifndef BATTERY_NORMALIZE_TEST
#include <klog.h>
#include <kprintf.h>
#include <ktime.h>
#include <io.h>
#endif
void battery_normalize(battery_info_t* b, bool present, u32 unit,
                       u32 full, const u32 bst[4]) {
    *b = (battery_info_t){.present=present, .unit=unit, .full=full,
        .remaining=BATTERY_UNKNOWN, .rate=BATTERY_UNKNOWN,
        .voltage=BATTERY_UNKNOWN, .percent=-1, .state=BAT_UNKNOWN};
    if (!present || !bst || unit > 1) return;
    b->valid = true;
    b->remaining = bst[2]; b->rate = bst[1]; b->voltage = bst[3];
    if (full && full != BATTERY_UNKNOWN && bst[2] != BATTERY_UNKNOWN) {
        u64 p = (u64)bst[2] * 100 / full;
        b->percent = p > 100 ? 100 : (i32)p;
    }
    if (bst[0] == BATTERY_UNKNOWN || (bst[0] & ~7u)) return;
    b->critical = (bst[0] & 4) != 0;
    switch (bst[0] & 3) {
    case 1: b->state = BAT_DISCHARGING; break;
    case 2: b->state = BAT_CHARGING; break;
    case 0: b->state = b->percent == 100 ? BAT_FULL : BAT_IDLE; break;
    }
}
#ifndef BATTERY_NORMALIZE_TEST
static battery_info_t cached[BATTERY_MAX];
static u32 count;
static i32 ac = -1;
static bool attempted, ready, busy, sampled, warned[BATTERY_MAX];
static u64 next_poll;
void battery_poll(void) {
    if (busy || time_boot_ns() < next_poll) return;
    busy = true;
    next_poll = time_boot_ns() + 30000000000ULL;
    if (!attempted) { attempted = true; ready = battery_acpi_init(); }
    if (ready) {
        battery_info_t sample[BATTERY_MAX];
        u32 n = 0; i32 source = -1;
        battery_firmware_poll();
        bool ok = battery_acpi_sample(sample, &n, &source);
        u64 flags = irq_save();
        count = n; ac = source; sampled = ok;
        for (u32 i = 0; i < n; i++) cached[i] = sample[i];
        irq_restore(flags);
        for (u32 i = 0; i < n; i++) {
            bool low = sample[i].present && sample[i].valid &&
                (sample[i].critical || (sample[i].state == BAT_DISCHARGING &&
                 sample[i].percent >= 0 && sample[i].percent <= 10));
            if (low && !warned[i]) klog(LOG_WARN, "battery%u: low battery (%d%%)%s\n",
                i, sample[i].percent, sample[i].critical ? " CRITICAL" : "");
            warned[i] = low;
        }
    }
    busy = false;
}
u64 battery_read_text(char* buf, u64 cap) {
    const char* names[] = {"unknown", "discharging", "charging", "full", "idle"};
    // procfs invokes us with interrupts disabled, so this snapshot is stable.
    u64 n = ksnprintf(buf, cap, "backend: ACPI\nstatus: %s\nac: %s\nbatteries: %u\n",
        !attempted ? "initializing" : !ready ? "unavailable" : !sampled ? "error" : "ready",
        ac < 0 ? "unknown" : ac ? "online" : "offline", count);
    for (u32 i = 0; i < count && n < cap; i++) {
        battery_info_t* b = &cached[i];
        int wrote = ksnprintf(buf+n, cap-n,
            "battery%u: %s\nstate: %s\npercent: %d\nunit: %s\nremaining: %u\nfull: %u\nrate: %u\nvoltage_mV: %u\ncritical: %u\n",
            i, !b->present ? "absent" : b->valid ? "present" : "unknown",
            names[b->state], b->percent, b->unit == 0 ? "mWh/mW" : b->unit == 1 ? "mAh/mA" : "unknown",
            b->remaining,b->full,b->rate,b->voltage,b->critical);
        n += wrote;
    }
    return n < cap ? n : cap ? cap-1 : 0;
}
#endif
