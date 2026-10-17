// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>

typedef struct {
    char status_msg[6];   // longest tag "DEBUG" + NUL
    int severity;
    int color;
} kernel_logs_t;

extern kernel_logs_t log_messages[4];

#define LOG_OKAY  (&log_messages[0])
#define LOG_WARN  (&log_messages[1])
#define LOG_DEBUG (&log_messages[2])
#define LOG_ERROR (&log_messages[3])

void klog(kernel_logs_t* log, const char* format, ...);

#define KMSG_CAPACITY 65536
void kmsg_putc(char c);
u64 kmsg_read(u64 off, void* buf, u64 len);
u64 kmsg_tail(void* buf, u64 len);
