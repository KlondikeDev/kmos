// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#define KUNIX_BUILD_DATE __DATE__
#define KUNIX_BUILD_TIME __TIME__
#define KUNIX_NAME     "kunix"
#define KUNIX_VERSION  "0.02"
#define KUNIX_CODENAME "kodiak"
#define KUNIX_ARCH     "x86_64"

#include <types.h>
#include <io.h>
#include <idt.h>
#include <serial.h>
#include <pit.h>
#include <keyboard.h>
#include <console.h>
#include <panic.h>
#include <cpuid.h>
#include <klog.h>
#include <pci.h>
#include <shell.h>
#include <limine.h>
#include <memory.h>
#include <cpuinit.h>

static volatile LIMINE_BASE_REVISION(3);

volatile struct limine_framebuffer_request fb_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

// Kernel Entry point!
void kernel_main(void) {
    init_serial();
    pmm_init();
    heap_init();
    framebuffer_init(); // Kunix, city of light!
    terminal_init();    // Kunix, city of night!
    cpu_init();
    
    klog(LOG_OKAY, "Booting Kunix Millennium Operating System...\n");
    klog(LOG_DEBUG, "Kernel Built at %s on %s: %s-%s (%s)\n", KUNIX_BUILD_TIME, KUNIX_BUILD_DATE, KUNIX_NAME, KUNIX_CODENAME, KUNIX_ARCH);
    
    idt_init();
    pit_init();
    klog(LOG_OKAY, "Programmable Interval Timer (PIT) set to 250hz (Square Wave)!\n");
    
    pci_list_devices();
    terminal_printf(BLACK, BLACK, "\n");
    
    cpuid_detect();

    terminal_printf(CYAN, BLACK, "Usable RAM: %uMB\n\n", get_usable_ram() / 1024 / 1024);

    terminal_printf(SILVER, BLACK, "kunix-kodiak v0.02 - Insane Membrane\n\n");
    terminal_printf(SILVER, BLACK, "Copyright (C)2025, 2026\nJoseph Jones - Released under the terms of the Kunix License v1.0 or later.\n\n");
    terminal_printf(SILVER, BLACK, "For more information on how to use Kunix, see the wiki:\nhttps://wiki.kunix.org\n");
    keyboard_init();

    shell_init();
    
	while(1) {
		__asm__ volatile ("hlt");
		if (command_ready) {
			command_ready = false;
			shell_process_command(input_buffer);
		}
	}
}

// Happy 15th to me! 12:00 AM!
