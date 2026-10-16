// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// The PS/2 mouse: the i8042's second ("aux") port, interrupting on IRQ 12.
//
// It sends 3-byte packets -- buttons and sign bits, then dx, then dy -- and
// /dev/input/mice hands them out unchanged, which is what Linux's mousedev
// does in its default (PS/2) mode. Programs may also write PS/2 commands
// to the device to probe it; like mousedev, we answer them ourselves
// (acknowledge everything, report a plain 3-byte mouse) rather than pass
// them to the hardware.

#include <mouse.h>
#include <io.h>
#include <proc.h>
#include <signal.h>
#include <errno.h>
#include <serial.h>

#define STATUS 0x64
#define DATA   0x60

#define RING 1536                      // a multiple of 3: whole packets
static u8 ring[RING];
static u32 head, count;                // next byte out; bytes queued
static u8 packet[3];
static u32 got;                        // bytes of the packet in progress

static bool wait_writable(void) {
    for (u32 i = 0; i < 100000; i++) if (!(inb(STATUS) & 0x02)) return true;
    return false;
}

static i32 read_data(void) {
    for (u32 i = 0; i < 100000; i++) if (inb(STATUS) & 0x01) return inb(DATA);
    return -1;
}

static void controller(u8 cmd) {
    wait_writable();
    outb(STATUS, cmd);
}

// A byte for the mouse itself, and its answer.
static i32 to_mouse(u8 b) {
    controller(0xD4);
    wait_writable();
    outb(DATA, b);
    return read_data();
}

void mouse_init(void) {
    u64 irq = irq_save();
    controller(0xA8);                  // enable the aux port
    controller(0x20);                  // the controller's configuration byte
    i32 cfg = read_data();
    if (cfg >= 0) {
        cfg |= 0x02;                   // aux interrupts on
        cfg &= ~0x20;                  // aux clock running
        controller(0x60);
        wait_writable();
        outb(DATA, (u8)cfg);
    }
    to_mouse(0xF6);                    // defaults
    bool ok = to_mouse(0xF4) == 0xFA;  // start reporting
    outb(0xA1, inb(0xA1) & ~0x10);     // unmask IRQ 12 on the slave PIC
    irq_restore(irq);
    kserialout(ok ? "mouse: PS/2 mouse ready\n" : "mouse: no PS/2 mouse answered\n");
}

static void queue(const u8* b, u32 n) {
    if (count + n > RING) return;      // nobody is reading: drop it
    for (u32 i = 0; i < n; i++) ring[(head + count + i) % RING] = b[i];
    count += n;
    sched_wake_obj(ring);
}

void mouse_irq(void) {
    u8 st = inb(STATUS);
    if ((st & 0x21) != 0x21) return;   // not the mouse's byte
    u8 b = inb(DATA);
    // The first byte of a packet always has bit 3 set; anything else here
    // means we lost step, so wait for one that does.
    if (got == 0 && !(b & 0x08)) return;
    packet[got++] = b;
    if (got < 3) return;
    got = 0;
    if (packet[0] & 0xC0) return;      // overflowed: the motion is garbage
    queue(packet, 3);
}

bool mouse_readable(void) {
    return count != 0;
}

i64 mouse_read(void* buf, u64 len, bool nonblock) {
    u8* out = buf;
    for (;;) {
        u64 irq = irq_save();
        if (count) {
            u64 n = len < count ? len : count;
            for (u64 i = 0; i < n; i++) out[i] = ring[(head + i) % RING];
            head = (head + (u32)n) % RING;
            count -= (u32)n;
            irq_restore(irq);
            return (i64)n;
        }
        irq_restore(irq);
        if (nonblock) return -EAGAIN;
        sched_block_on(ring);
        if (signal_pending(proc_current())) return -EINTR;
    }
}

i64 mouse_write(const void* buf, u64 len) {
    const u8* c = buf;
    u64 irq = irq_save();
    for (u64 i = 0; i < len; i++) {
        switch (c[i]) {
            case 0xFF: queue((const u8[]){ 0xFA, 0xAA, 0x00 }, 3); break;    // reset: ok, passed, id 0
            case 0xF2: queue((const u8[]){ 0xFA, 0x00 }, 2); break;          // identify: a plain mouse
            case 0xE9: queue((const u8[]){ 0xFA, 0x00, 0x02, 0x64 }, 4); break;  // status
            default:   queue((const u8[]){ 0xFA }, 1); break;                // everything else: ok
        }
    }
    irq_restore(irq);
    return (i64)len;
}
