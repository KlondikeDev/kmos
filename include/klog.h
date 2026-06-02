// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>

typedef struct {
    char status_msg[5];
    int severity;
    int color;
} kernel_logs_t;

extern kernel_logs_t log_messages[4];

#define LOG_OKAY  (&log_messages[0])
#define LOG_WARN  (&log_messages[1])
#define LOG_DEBUG (&log_messages[2])
#define LOG_ERROR (&log_messages[3])

void klog(kernel_logs_t* log, const char* format, ...);
