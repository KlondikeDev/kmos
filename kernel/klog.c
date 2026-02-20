// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <types.h>
#include <console.h>
#include <serial.h>
#include <klog.h>
#include <pit.h>
#include <stdarg.h>
#include <itoa.h>


kernel_logs_t log_messages[4] = {
    {"OKAY", 0, GREEN},
    {"WARN", 1, YELLOW},
    {"DEBUG", 2, BLUE},
    {"ERROR", 3, RED}
};

void klog(kernel_logs_t* log, const char* format, ...) {
    va_list args;
    va_start(args, format);
    kserialout(format);
    kserialout("\n");

    uint32_t seconds = ticks / 250;
    uint32_t microseconds = (ticks % 250) * 4000;
    terminal_printf(SILVER, BLACK, "[%4d.%06d] ", seconds, microseconds);
    terminal_printf(SILVER, BLACK, "[");
    terminal_printf(log->color, BLACK, "%s", log->status_msg);
    terminal_printf(SILVER, BLACK, "] ");
    while(*format) {
        if (*format == '%') {
            format++;
            
            char pad_char = ' ';
            if (*format == '0') {
                pad_char = '0';
                format++;
            }
            
            int width = 0;
            while (*format >= '0' && *format <= '9') {
                width = width * 10 + (*format - '0');
                format++;
            }
            
            switch (*format) {
                case 'd': {
                    char buffer[100];
                    int32_t val = va_arg(args, int);
                    itoa(val, buffer);
                    print_padded(buffer, width, pad_char);
                    break;
                }
                case 'u': {
                    char buffer[100];
                    uint32_t val = va_arg(args, unsigned int);
                    utoa(val, buffer, 10);
                    print_padded(buffer, width, pad_char);
                    break;
                }
                case 'x': {
                    char buffer[100];
                    uint32_t val = va_arg(args, unsigned int);
                    utoa(val, buffer, 16);
                    print_padded(buffer, width, pad_char);
                    break;
                }
                case 's': {
                    char* str = va_arg(args, char*);
                    print_padded(str, width, ' ');
                    break;
                }
                case '%': {
                    terminal_putchar('%');
                    break;
                }
                default: {
                    terminal_putchar('%');
                    terminal_putchar(*format);
                }
            }
        } else {
            terminal_putchar(*format);
        }
        format++;
    }

    va_end(args);

}