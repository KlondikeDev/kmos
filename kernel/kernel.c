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
#include <mouse.h>
#include <disk.h>
#include <nvme.h>
#include <gpt.h>
#include <klibc/string.h>
#include <console.h>
#include <panic.h>
#include <cpuid.h>
#include <klog.h>
#include <pci.h>
#include <shell.h>
#include <limine.h>
#include <memory.h>
#include <cpuinit.h>
#include <vt.h>
#include <primemeridian.h>
#include <procfs.h>
#include <tmpfs.h>
#include <ata.h>
#include <fat32.h>
#include <vfs.h>
#include <ufs2.h>
#include <gdt.h>
#include <proc.h>
#include <usermode.h>
#include <errno.h>
#include <file.h>
#include <tty.h>
#include <rtc.h>
#include <devfs.h>
#include <acpi.h>
#include <battery.h>

// Limine nuke!
static volatile LIMINE_BASE_REVISION(3);

volatile struct limine_framebuffer_request fb_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

// Kernel Entry point!
static volatile struct limine_kernel_file_request boot_file = {
    .id=LIMINE_KERNEL_FILE_REQUEST,.revision=0
};
// -1 means no selector; -2 is an explicit invalid/missing root. Never silently
// boot a different filesystem after the operator named one.
static i32 boot_disk(const char* key) {
    const char* line=boot_file.response && boot_file.response->kernel_file ? boot_file.response->kernel_file->cmdline : NULL;
    u64 n=strlen(key); i32 chosen=-1;
    for(const char* p=line;p && *p;) {
        while(*p==' ')p++;
        const char* end=p;while(*end && *end!=' ')end++;
        if((u64)(end-p)>=n && !strncmp(p,key,n)) {
            if(chosen!=-1 || end-p!=(i64)n+45 || strncmp(p+n,"PARTUUID=",9))return -2;
            char text[37];
            for(u32 i=0;i<36;i++)text[i]=p[n+9+i];
            text[36]=0;
            u8 uuid[16];if(!gpt_guid_parse(text,uuid))return -2;
            chosen=disk_find_uuid(uuid);if(chosen<0)return -2;
        }
        p=end;
    }
    return chosen;
}

void kernel_main(void) {
    init_serial();
    pmm_init();
    heap_init();
    vmm_init();
    framebuffer_init(); // Kunix, city of light!
    terminal_init();    // Kunix, city of night!
    vt_init();
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
    pm_init();          // Prime Meridian: the `syscall` instruction's MSRs

    // Before idt_init: that turns interrupts on, and the very first timer
    // tick goes through the scheduler.
    sched_init();

    idt_init();
    pit_init();
    klog(LOG_OKAY, "Programmable Interval Timer (PIT) set to 250hz (Square Wave)!\n");
    rtc_init();
    acpi_init();
    disk_cache_init();
    
    pci_list_devices();
    nvme_init();
    disk_scan_partitions();
    bool fat = identify_drive(0xA0) && fat32_init();
    // The root is UFS2: an image the bootloader loaded into memory if there
    // is one (the live image, for machines whose disks Kunix cannot drive
    // yet), else the primary slave, with the FAT32 boot disk beside it at
    // /fat. Without either, FAT32 is the root, as it used to be. Home
    // directories have a UFS2 disk of their own on the secondary slave
    // (the secondary master is the CD-ROM we booted from).
    void* root = NULL;
    i32 root_disk=boot_disk("root=");
    i32 home_disk=boot_disk("home=");
    if(root_disk>=0 && home_disk>=0 && disk_overlaps((u8)root_disk,(u8)home_disk)) {
        klog(LOG_ERROR,"boot: root/home devices overlap; refusing selected filesystems\n");
        root_disk=home_disk=-2;
    }
    i32 ram=ramdisk_from_module("root.img");
    if(ram>=0) disk_set_install_source(ram);
    if(root_disk>=0) root=ufs2_mount((u8)root_disk);
    else if(root_disk==-1) {
        if(ram>=0) { root=ufs2_mount((u8)ram);if(root)root_disk=ram; }
        if(!root && identify_drive(0xB0)) { root=ufs2_mount(1);if(root)root_disk=1; }
    }
    if(root) {
        vfs_mount_with("/",&ufs2_vfs_ops,root);disk_set_mounted((u8)root_disk,true);
        klog(LOG_OKAY,"boot: root disk %u\n",(u32)root_disk);
        if(fat) { vfs_mount("/fat",&fat32_vfs_ops);disk_set_mounted(0,true); }
    } else if(root_disk==-1 && fat) {
        vfs_mount("/",&fat32_vfs_ops);disk_set_mounted(0,true);
    } else {
        klog(LOG_ERROR,"no root filesystem%s\n",root_disk==-2?": invalid PARTUUID selector":"");
    }
    if(home_disk==-1 && ata_present(3))home_disk=3;
    if(root && home_disk>=0 && disk_overlaps((u8)root_disk,(u8)home_disk)) {
        klog(LOG_ERROR,"boot: root/home devices overlap; refusing home\n");home_disk=-2;
    }
    void* home=home_disk>=0 ? ufs2_mount((u8)home_disk) : NULL;
    if(home) { vfs_mount_with("/home",&ufs2_vfs_ops,home);disk_set_mounted((u8)home_disk,true); }
    else if(home_disk==-2)klog(LOG_WARN,"boot: invalid home PARTUUID selector\n");
    vfs_mount("/dev", &devfs_vfs_ops);
    vfs_mount("/proc", &procfs_vfs_ops);
    vfs_mount("/tmp", &tmpfs_vfs_ops);
    tty_init();

    // Give the kernel task stdin/stdout/stderr on the terminal. Every
    // process inherits descriptors from its parent, so this is where the
    // whole system's 0, 1 and 2 come from.
    file_t* console;
    proc_current()->ctty = &tty_console;
    if (file_open("/dev/console", O_RDWR, &console) == 0) {
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
    mouse_init();

    // The console belongs to the ring 3 shell when there is one on disk. The
    // kernel shell is the fallback, and takes over again whenever no user
    // processes are left (e.g. after `exit` in the ring 3 shell).
    // Interrupts stay off until the log line is out, so the shell's banner
    // cannot overtake it.
    //
    // The first shell found wins: zsh (through Prime Meridian) as a login
    // shell, then Kunix's own ksh. (Until Habilis takes over as init.)
    static const char* const shells[][2] = {
        { "/sbin/init", "/sbin/init" },                 // Habilis
        { "/bin/zsh", "-zsh" },
        { "/bin/sh.elf", "/bin/sh.elf" },
        { "/fat/BIN/SH.ELF", "/fat/BIN/SH.ELF" },
        { "/BIN/SH.ELF", "/BIN/SH.ELF" },                 // FAT32 as the root
    };
    u64 flags = irq_save();
    i32 init_pid = -ENOENT;
    user_args_t* args = kmalloc(sizeof(user_args_t));
    for (u32 i = 0; args && i < sizeof(shells) / sizeof(shells[0]); i++) {
        args->argc = 1;
        args->argv[0] = shells[i][1];
        args->envc = 0;
        init_pid = usermode_spawn(shells[i][0], args, NULL);
        if (init_pid >= 0) {
            // It leads its own process group; make that the terminal's
            // foreground, so Ctrl+C reaches it and whatever it runs.
            tty_console.fg_pgid = init_pid;
            klog(LOG_OKAY, "Started %s as pid %u\n", shells[i][0], (u32)init_pid);
            break;
        }
    }
    if (args) kfree(args);
    if (init_pid < 0) klog(LOG_WARN, "No shell on disk, starting the kernel shell\n");
    irq_restore(flags);

    // From here on this is PID 0, the kernel task. It only runs when no
    // process is ready, so this loop doubles as the idle loop.
    bool kernel_shell = false;
    static char line[TTY_LINE_MAX];
	while(1) {
		__asm__ volatile ("hlt");
		disk_cache_poll();
		battery_poll();
		sched_maintenance_done();
		sched_reap_orphans();
		if (sched_user_procs()) continue;     // keyboard input is theirs

		if (!kernel_shell) {
			kernel_shell = true;
			tty_init();                       // sane settings, whatever the last program left
			while (tty_kernel_getline(line, sizeof(line))) { }   // drop stale input
			shell_init();
		}
		if (tty_kernel_getline(line, sizeof(line))) {
			shell_process_command(line);
		}
	}
}

// Happy 15th to me! 12:00 AM!
