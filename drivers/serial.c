// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <io.h>
#include <console.h>
#include <serial.h>
#include <klog.h>

#define COM1 0x3f8          // COM1

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

   // Check if serial is faulty (i.e: not same byte as sent)
   if(inb(COM1 + 0) != 0xAE) {
        klog(LOG_WARN, "Serial faulty or not detected...\n");
        faulty = true;
   }

   // If serial is not faulty set it in normal operation mode
   // (not-loopback with IRQs enabled and OUT#1 and OUT#2 bits enabled)
   if(!faulty){
        outb(COM1 + 4, 0x0F);
        klog(LOG_OKAY, "COM1 Enabled at port 0x%x\n", COM1);
   }
   return 0;
}

void kserialputchar(char c) {
    if(faulty){
        return;
    }
    else{
    if (c == '\n') {
        kserialputchar('\r'); // Carriage return before newline
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