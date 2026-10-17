// This file has been generated in whole or in part by a Large Language Model
#pragma once
#include <types.h>
#define BATTERY_UNKNOWN 0xffffffffu
#define BATTERY_MAX 8
// Capacities and rate use mWh/mW (unit 0) or mAh/mA (unit 1).
typedef struct {
    bool present, valid, critical;
    u32 unit, remaining, full, rate, voltage;
    i32 percent;
    enum { BAT_UNKNOWN, BAT_DISCHARGING, BAT_CHARGING, BAT_FULL, BAT_IDLE } state;
} battery_info_t;
void battery_normalize(battery_info_t* out, bool present, u32 unit,
                       u32 full, const u32 bst[4]);
void battery_poll(void); // kernel task only, interrupts enabled; self-throttled
u64 battery_read_text(char* buf, u64 capacity); // cached, never runs AML
// Firmware adapter, called only by battery_poll.
bool battery_acpi_init(void);
bool battery_acpi_sample(battery_info_t* out, u32* count, i32* ac);
void battery_firmware_poll(void);
