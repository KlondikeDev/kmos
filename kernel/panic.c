// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <boxes.h>
#include <console.h>
#include <serial.h>
#include <idt.h>

void panic(const char* reason, registers_t* regs) {
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    terminal_printf(WHITE, BLACK, "\npanic: %s. execution halted.\n\n", reason);
    terminal_printf(WHITE, BLACK, BOX_TL " registers: " BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_TR "\n");
    terminal_printf(WHITE, BLACK, BOX_V " rax=0x%016lx rbx=0x%016lx rcx=0x%016lx rdx=0x%016lx " BOX_V "\n", regs->rax, regs->rbx, regs->rcx, regs->rdx);
    terminal_printf(WHITE, BLACK, BOX_V " rsi=0x%016lx rdi=0x%016lx rbp=0x%016lx                        " BOX_V "\n", regs->rsi, regs->rdi, regs->rbp);
    terminal_printf(WHITE, BLACK, BOX_V " r8 =0x%016lx r9 =0x%016lx r10=0x%016lx r11=0x%016lx " BOX_V "\n", regs->r8, regs->r9, regs->r10, regs->r11);
    terminal_printf(WHITE, BLACK, BOX_V " r12=0x%016lx r13=0x%016lx r14=0x%016lx r15=0x%016lx " BOX_V "\n", regs->r12, regs->r13, regs->r14, regs->r15);
    terminal_printf(WHITE, BLACK, BOX_V " rip=0x%016lx eflags=0x%016lx cs=0x%04lx                                  " BOX_V "\n", regs->rip, regs->eflags, regs->cs);
    terminal_printf(WHITE, BLACK, BOX_V " CR0=0x%016lx CR2=0x%016lx CR3=0x%016lx CR4=0x%016lx " BOX_V "\n", cr0, cr2, cr3, cr4);
    terminal_printf(WHITE, BLACK, BOX_V " exception: 0x%02lx %-75s " BOX_V "\n", regs->int_no, "");
    terminal_printf(WHITE, BLACK, BOX_V " error_code: 0x%016lx                                                              " BOX_V "\n", regs->err_code);
    terminal_printf(WHITE, BLACK, BOX_BL BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_BR "\n\n");
    terminal_printf(WHITE, BLACK, "Thank you for using Kunix. It is now safe to restart your computer.\n");

    kserialout("KERNEL PANIC! ");
    kserialout(reason);

    while(1) { __asm__ volatile ("hlt"); }
}