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
#include <io.h>

// Retain early boot and serial-only diagnostics even without a UART. The
// ring needs no allocator and never calls back into either output path.
static char messages[KMSG_CAPACITY];
static u64 message_end;

void kmsg_putc(char c) {
    u64 irq = irq_save();
    messages[message_end++ % KMSG_CAPACITY] = c;
    irq_restore(irq);
}

u64 kmsg_read(u64 off, void* buf, u64 len) {
    u64 irq = irq_save();
    u64 n = message_end < KMSG_CAPACITY ? message_end : KMSG_CAPACITY;
    u64 start = message_end - n;
    if (off >= n) len = 0;
    else if (len > n - off) len = n - off;
    for (u64 i = 0; i < len; i++) ((char*)buf)[i] = messages[(start + off + i) % KMSG_CAPACITY];
    irq_restore(irq);
    return len;
}

u64 kmsg_tail(void* buf, u64 len) {
    u64 irq = irq_save();
    u64 n = message_end < KMSG_CAPACITY ? message_end : KMSG_CAPACITY;
    if (len > n) len = n;
    u64 result = kmsg_read(n - len, buf, len);
    irq_restore(irq);
    return result;
}


kernel_logs_t log_messages[4] = {
    {"OKAY", 0, GREEN},
    {"WARN", 1, YELLOW},
    {"DEBUG", 2, BLUE},
    {"ERROR", 3, RED}
};

void klog(kernel_logs_t* log, const char* format, ...) {
    va_list args;
    va_list serial_args;
    va_start(args, format);
    va_copy(serial_args, args);
    vkserialoutf(format, serial_args);
    va_end(serial_args);
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

            // Length modifier: %l and %ll both mean "the argument is 64-bit".
            // Without this a 64-bit value would be read as an int and the rest
            // of the argument list would be misaligned from there on.
            int is_long = 0;
            while (*format == 'l') {
                is_long = 1;
                format++;
            }

            if (*format == '\0') {   // format ends in '%': don't run past the terminator
                terminal_putchar('%');
                break;
            }

            switch (*format) {
                case 'd': {
                    char buffer[100];
                    if (is_long) {
                        itoa64(va_arg(args, i64), buffer);
                    } else {
                        itoa(va_arg(args, int), buffer);
                    }
                    print_padded(buffer, width, pad_char);
                    break;
                }
                case 'u': {
                    char buffer[100];
                    if (is_long) {
                        utoa64(va_arg(args, u64), buffer, 10);
                    } else {
                        utoa(va_arg(args, unsigned int), buffer, 10);
                    }
                    print_padded(buffer, width, pad_char);
                    break;
                }
                case 'x': {
                    char buffer[100];
                    if (is_long) {
                        utoa64(va_arg(args, u64), buffer, 16);
                    } else {
                        utoa(va_arg(args, unsigned int), buffer, 16);
                    }
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
    terminal_flush();   // body text goes through terminal_putchar, which never flushes

}
