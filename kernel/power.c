// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <types.h>
#include <power.h>
#include <vfs.h>
#include <io.h>
#include <console.h>
#include <vt.h>
#include <klog.h>

char kernel_hostname[65] = "kunix";

__attribute__((noreturn)) void power_down(u32 how) {
    __asm__ volatile("cli");
    vfs_shutdown_all();

    if (how == POWER_OFF) {
        klog(LOG_OKAY, "power: switching off\n");
        // ACPI sleep state S5 through the PM1a control register, wherever
        // the (virtual) chipset keeps it: QEMU's PIIX4 and Q35, Bochs and
        // older QEMU, VirtualBox.
        outw(0x604, 0x2000);
        outw(0xB004, 0x2000);
        outw(0x4004, 0x3400);
    } else if (how == POWER_RESTART) {
        klog(LOG_OKAY, "power: restarting\n");
        // Pulse the CPU reset line through the keyboard controller...
        for (int i = 0; i < 100000 && (inb(0x64) & 0x02); i++) { }
        outb(0x64, 0xFE);
        // ...or, failing that, triple-fault: no IDT, then an exception.
        struct { u16 limit; u64 base; } __attribute__((packed)) none = { 0, 0 };
        __asm__ volatile("lidt %0; int3" :: "m"(none));
    }
    terminal_printf(SILVER, BLACK, "\nThe system is halted. It is now safe to turn off the machine.\n");
    vt_flush();
    for (;;) __asm__ volatile("cli; hlt");
}
