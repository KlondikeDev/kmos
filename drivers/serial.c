// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <io.h>
#include <itoa.h>
#include <stdarg.h>
#include <serial.h>
#include <klog.h>

volatile bool faulty = false;

int init_serial() {
   outb(COM1 + 1, 0x01);    // Enable Interrupts
   outb(COM1 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
   outb(COM1 + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
   outb(COM1 + 1, 0x00);    //                  (hi byte)
   outb(COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit
   outb(COM1 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
   outb(COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
   outb(COM1 + 4, 0x1E);    // Set in loopback mode, test the serial chip
   outb(COM1 + 0, 0xAE);    // Test serial chip (send byte 0xAE and check if serial returns same byte)

   // A UART need not finish the loopback byte before the next instruction.
   // UEFI boots exposed this race: do not permanently disable a working
   // port just because its receive register was checked too early.
   u32 tries = 100000;
   while (!(inb(COM1 + 5) & 1) && --tries) { }
   // Check if serial is faulty (i.e: not same byte as sent)
   if(!tries || inb(COM1 + 0) != 0xAE) {
        faulty = true;
   }

   // If serial is not faulty set it in normal operation mode
   // (not-loopback with IRQs enabled and OUT#1 and OUT#2 bits enabled)
   if(!faulty){
        outb(COM1 + 4, 0x0F);
   }
   return 0;
}

void kserialputchar(char c) {
    kmsg_putc(c);
    if(faulty){
        return;
    }
    else{
    if (c == '\n') {
        // The UART needs CRLF; the in-memory log retains the original LF.
        while (!(inb(COM1 + 5) & 0x20));
        outb(COM1, '\r');
    }

    while (!(inb(COM1 + 5) & 0x20));

    outb(COM1, c);
}
}

void kserialout(const char *s) {
    while (*s) {
        kserialputchar(*s++);
    }

}

static void kserialout_padded(const char* str, int width, char pad_char) {
    int len = 0;
    const char* p = str;
    while (*p++) {
        len++;
    }
    int padding = width - len;
    if (padding > 0) {
        for (int i = 0; i < padding; i++) {
            kserialputchar(pad_char);
        }
    }
    kserialout(str);
}

void kserialoutf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vkserialoutf(format, args);
    va_end(args);
}

void vkserialoutf(const char* format, va_list args) {
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
                kserialputchar('%');
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
                    kserialout_padded(buffer, width, pad_char);
                    break;
                }
                case 'u': {
                    char buffer[100];
                    if (is_long) {
                        utoa64(va_arg(args, u64), buffer, 10);
                    } else {
                        utoa(va_arg(args, unsigned int), buffer, 10);
                    }
                    kserialout_padded(buffer, width, pad_char);
                    break;
                }
                case 'x': {
                    char buffer[100];
                    if (is_long) {
                        utoa64(va_arg(args, u64), buffer, 16);
                    } else {
                        utoa(va_arg(args, unsigned int), buffer, 16);
                    }
                    kserialout_padded(buffer, width, pad_char);
                    break;
                }
                case 's': {
                    char* str = va_arg(args, char*);
                    kserialout_padded(str, width, ' ');
                    break;
                }
                case '%': {
                    kserialputchar('%');
                    break;
                }
                default: {
                    kserialputchar('%');
                    kserialputchar(*format);
                }
            }
        } else {
            kserialputchar(*format);
        }
        format++;
    }
}
