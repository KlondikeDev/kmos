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
#include <ata.h>
#include <fat32.h>
#include <vfs.h>
#include <ufs2.h>
#include <gdt.h>
#include <proc.h>
#include <usermode.h>
#include <file.h>
#include <tty.h>
#include <rtc.h>
#include <devfs.h>

// Limine nuke!
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
    vmm_init();
    framebuffer_init(); // Kunix, city of light!
    terminal_init();    // Kunix, city of night!
    cpu_init();
    if(faulty == true) {
        klog(LOG_WARN, "Serial port faulty or not present...\n");
    } else {
        klog(LOG_OKAY, "COM1 Enabled at port 0x%x\n", COM1);
    }
    klog(LOG_OKAY, "Booting Kunix Millennium Operating System...\n");
    klog(LOG_DEBUG, "Kernel Built at %s on %s: %s-%s (%s)\n", KUNIX_BUILD_TIME, KUNIX_BUILD_DATE, KUNIX_NAME, KUNIX_CODENAME, KUNIX_ARCH);
    
    // Must precede idt_init: the IDT gates reference our own kernel code
    // selector, and the TSS has to exist before anything can trap out of ring 3.
    gdt_init();

    // Before idt_init: that turns interrupts on, and the very first timer
    // tick goes through the scheduler.
    sched_init();

    idt_init();
    pit_init();
    klog(LOG_OKAY, "Programmable Interval Timer (PIT) set to 250hz (Square Wave)!\n");
    rtc_init();
    
    pci_list_devices();
    identify_drive(0xA0);
    fat32_init();
    vfs_mount("/", &fat32_vfs_ops);
    // A second IDE disk (primary slave) holding UFS2 appears at /ufs.
    if (identify_drive(0xB0) && ufs2_mount(1)) {
        vfs_mount("/ufs", &ufs2_vfs_ops);
    }
    vfs_mount("/dev", &devfs_vfs_ops);
    tty_init();

    // Give the kernel task stdin/stdout/stderr on the terminal. Every
    // process inherits descriptors from its parent, so this is where the
    // whole system's 0, 1 and 2 come from.
    file_t* console;
    if (file_open("/dev/tty", O_RDWR, &console) == 0) {
        process_t* k = proc_current();
        fd_install(k, console, 0);
        file_ref(console); fd_install(k, console, 1);
        file_ref(console); fd_install(k, console, 2);
    }

    terminal_printf(BLACK, BLACK, "\n");
    
    cpuid_detect();

    terminal_printf(CYAN, BLACK, "Usable RAM: %uMB\n\n", get_usable_ram() / 1024 / 1024);

    terminal_printf(SILVER, BLACK, "kunix-kodiak v0.02 - Insane Membrane\n\n");
    terminal_printf(SILVER, BLACK, "Copyright (C) 2025, 2026\nJoseph Jones - Released under the terms of the Kunix License v1.0 or later.\n\n");
    terminal_printf(SILVER, BLACK, "For more information on how to use Kunix, see the wiki:\nhttps://wiki.kunix.org\n");
    keyboard_init();

    // The console belongs to the ring 3 shell when there is one on disk. The
    // kernel shell is the fallback, and takes over again whenever no user
    // processes are left (e.g. after `exit` in the ring 3 shell).
    // Interrupts stay off until the log line is out, so the shell's banner
    // cannot overtake it.
    u64 flags = irq_save();
    i32 init_pid = usermode_spawn("/BIN/SH.ELF", NULL, NULL);
    if (init_pid < 0) {
        klog(LOG_WARN, "No /BIN/SH.ELF on disk, starting the kernel shell\n");
    } else {
        // It leads its own process group; make that the terminal's
        // foreground, so Ctrl+C reaches it and whatever it runs.
        tty_set_foreground(init_pid);
        klog(LOG_OKAY, "Started /BIN/SH.ELF as pid %u\n", (u32)init_pid);
    }
    irq_restore(flags);

    // From here on this is PID 0, the kernel task. It only runs when no
    // process is ready, so this loop doubles as the idle loop.
    bool kernel_shell = false;
    static char line[TTY_LINE_MAX];
	while(1) {
		__asm__ volatile ("hlt");
		sched_reap_orphans();
		if (sched_user_procs()) continue;     // keyboard input is theirs

		if (!kernel_shell) {
			kernel_shell = true;
			tty_set_mode(TTY_MODE_CANON | TTY_MODE_ECHO | TTY_MODE_ISIG);
			while (tty_kernel_getline(line, sizeof(line))) { }   // drop stale input
			shell_init();
		}
		if (tty_kernel_getline(line, sizeof(line))) {
			shell_process_command(line);
		}
	}
}

// Happy 15th to me! 12:00 AM!
