// This file has been generated in whole or in part by a Large Language Model
#pragma once
#include <types.h>
u32 wifi_control_size(u32 request);
i64 wifi_control_ioctl(u32 request, void* argument);
// PID 0 runs the queued operation with interrupts enabled.
void wifi_control_poll(void);
