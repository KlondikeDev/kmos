// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <shell.h>
#include <console.h>
#include <klibc/string.h>
#include <keyboard.h>
#include <cpuid.h>
#include <pci.h>
#include <panic.h>
#include <idt.h>
#include <io.h>
#include <types.h>
#include <security.h>
#include <memory.h>
#include <fat32.h>
#include <vfs.h>
#include <tty.h>
#include <proc.h>
#include <usermode.h>

static void shell_prompt(void) {
    terminal_printf(SILVER, BLACK, "kunix:%s|> ", proc_current()->cwd);
    prompt_x = cursor_x;
}

void shell_init(void) {
    terminal_printf(WHITE, BLACK, "\nType 'help' for commands\n\n");
    shell_prompt();
}

void shell_process_command(const char* cmd) {
    terminal_putchar('\n');
    
    if (cmd[0] == '\0') {
        shell_prompt();
        return;
    }
    
    if(strcmp(cmd, "help") == 0) {
        terminal_printf(SILVER, BLACK, "\nCommands:\n");
        terminal_printf(SILVER, BLACK, "help    - Displays list of commands\n");
        terminal_printf(SILVER, BLACK, "echo    - Prints string\n");
        terminal_printf(SILVER, BLACK, "clear   - Clear the screen\n");
        terminal_printf(SILVER, BLACK, "sysinfo - Displays system information\n");
        terminal_printf(SILVER, BLACK, "reboot  - Reboots the system\n");
        terminal_printf(SILVER, BLACK, "pci     - Lists PCI devices\n");
        terminal_printf(SILVER, BLACK, "ls      - Lists files in a directory\n");
        terminal_printf(SILVER, BLACK, "mounts  - Lists mounted filesystems\n");
        terminal_printf(SILVER, BLACK, "cd      - Changes the working directory\n");
        terminal_printf(SILVER, BLACK, "pwd     - Prints the working directory\n");
        terminal_printf(SILVER, BLACK, "cat     - Prints a file's contents\n");
        terminal_printf(SILVER, BLACK, "testssp - Tests stack-smashing protection\n");
        terminal_printf(SILVER, BLACK, "usermode - Runs a ring 3 program ('stack' or 'fault' for variants)\n");
        terminal_printf(SILVER, BLACK, "run     - Runs an ELF executable from disk in ring 3\n");
        terminal_printf(SILVER, BLACK, "panic   - Initiates kernel panic\n\n");
    } else if(strcmp(cmd, "clear") == 0) {
        terminal_clear();
        cursor_x = 0;
        cursor_y = 0;
    } else if(strcmp(cmd, "sysinfo") == 0) {
        terminal_putchar('\n');
        cpuid_detect();
        terminal_printf(CYAN, BLACK, "Usable RAM: %uMB\n\n", get_usable_ram() / 1024 / 1024);
        terminal_printf(SILVER, BLACK, "Kunix 0.02-kodiak (x86_64)\n");
        terminal_putchar('\n');
    } else if(strcmp(cmd, "panic") == 0){
        __asm__ volatile("int $0x81");
    } else if(strcmp(cmd, "reboot") == 0) {
        while (inb(0x64) & 0x02);
        outb(0x64, 0xFE);
    } else if(strcmp(cmd, "pci") == 0){
        pci_list_devices();
    } else if(strncmp(cmd, "echo ", 5) == 0){
        const char* args = cmd + 5;
        terminal_printf(WHITE, BLACK, "%s\n", args);
    } else if (strcmp(cmd, "testssp") == 0) {
            test_ssp();
            return;
    } else if(strcmp(cmd, "usermode") == 0){
        usermode_demo();
    } else if(strcmp(cmd, "usermode stack") == 0){
        usermode_stack_demo();
    } else if(strcmp(cmd, "usermode fault") == 0){
        usermode_fault_demo();
    } else if(strncmp(cmd, "run ", 4) == 0){
        char path[256];
        size_t n = 0;
        for (const char* p = cmd + 4; *p && n < sizeof(path) - 1; p++) path[n++] = *p;
        path[n] = '\0';
        u64 flags = irq_save();
        i32 pid = usermode_spawn(path, NULL, NULL);
        if (pid > 0) tty_console.fg_pgid = pid;
        irq_restore(flags);
        if (pid < 0) {
            terminal_printf(GREY, BLACK, "run: %s: cannot execute\n", path);
        } else {
            usermode_run_and_wait(pid);
        }
    } else if(strcmp(cmd, "ls") == 0 || strncmp(cmd, "ls ", 3) == 0){
        const char* dir = cmd[2] ? cmd + 3 : ".";
        char name[VFS_NAME_MAX];
        vnode_t node;
        i32 r;
        for (u32 i = 0; (r = vfs_readdir_path(dir, i, name, &node)) == 1; i++) {
            if (node.type == VN_DIR) terminal_printf(CYAN, BLACK, "%s/\n", name);
            else terminal_printf(SILVER, BLACK, "%s\t%u bytes\n", name, (u32)node.size);
        }
        if (r < 0) terminal_printf(GREY, BLACK, "ls: %s: no such directory\n", dir);
    } else if(strcmp(cmd, "mounts") == 0){
        vfs_list_mounts();
    } else if(strcmp(cmd, "pwd") == 0){
        terminal_printf(SILVER, BLACK, "%s\n", proc_current()->cwd);
    } else if(strncmp(cmd, "cd ", 3) == 0){
        const char* path = cmd + 3;
        if(vfs_chdir(path) < 0){
            terminal_printf(GREY, BLACK, "cd: %s: no such directory\n", path);
        }
    } else if(strncmp(cmd, "cat ", 4) == 0){
        const char* fname = cmd + 4;
        char* fbuf = kmalloc(4097);
        if(fbuf == NULL){
            terminal_printf(RED, BLACK, "cat: out of memory\n");
        } else {
            i32 n = vfs_read_file(fname, fbuf, 4096);
            if(n < 0){
                terminal_printf(GREY, BLACK, "cat: %s: no such file\n", fname);
            } else {
                fbuf[n] = '\0';
                terminal_printf(WHITE, BLACK, "%s\n", fbuf);
            }
            kfree(fbuf);
        }
    } else {
        terminal_printf(GREY, BLACK, "command not found: %s\n", cmd);
    }

    shell_prompt();
}
