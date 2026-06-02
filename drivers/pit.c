// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <types.h>
#include <serial.h>
#include <pit.h>
#include <io.h>

// Hey, I took my own advice!
#define DATA_PORT_0 0x40
#define COMMAND_PORT 0x43

void pit_init() {

    int base_frequency = 1193180; // PIT base frequency
    int divisor = base_frequency / 250;

    outb(COMMAND_PORT, 0x36);

    outb(DATA_PORT_0, divisor & 0xFF);          // Low byte
    outb(DATA_PORT_0, (divisor >> 8) & 0xFF);   // High byte

    kserialout("PIT set to 250Hz, Square wave!\n");

}