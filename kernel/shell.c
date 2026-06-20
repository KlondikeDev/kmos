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

static void shell_prompt(void) {
    terminal_printf(SILVER, BLACK, "kunix:%s|> ", fat32_pwd());
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
        terminal_printf(SILVER, BLACK, "ls      - Lists files in the working directory\n");
        terminal_printf(SILVER, BLACK, "cd      - Changes the working directory\n");
        terminal_printf(SILVER, BLACK, "pwd     - Prints the working directory\n");
        terminal_printf(SILVER, BLACK, "cat     - Prints a file's contents\n");
        terminal_printf(SILVER, BLACK, "testssp - Tests stack-smashing protection\n");
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
    } else if(strcmp(cmd, "ls") == 0){
        fat32_list();
    } else if(strcmp(cmd, "pwd") == 0){
        terminal_printf(SILVER, BLACK, "%s\n", fat32_pwd());
    } else if(strncmp(cmd, "cd ", 3) == 0){
        const char* path = cmd + 3;
        if(!fat32_chdir(path)){
            terminal_printf(GREY, BLACK, "cd: %s: no such directory\n", path);
        }
    } else if(strncmp(cmd, "cat ", 4) == 0){
        const char* fname = cmd + 4;
        char* fbuf = kmalloc(4097);
        if(fbuf == NULL){
            terminal_printf(RED, BLACK, "cat: out of memory\n");
        } else {
            i32 n = fat32_read_file(fname, fbuf, 4096);
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
