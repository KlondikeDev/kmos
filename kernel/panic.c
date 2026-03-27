// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <boxes.h>
#include <console.h>
#include <serial.h>
#include <idt.h>

void panic(const char* reason, registers_t* regs) {
    terminal_printf(WHITE, BLACK, "\npanic: %s. execution halted.\n\n", reason);
    terminal_printf(WHITE, BLACK, BOX_TL " registers: " BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_TR "\n");
    terminal_printf(WHITE, BLACK, BOX_V " rax=0x%016lx rbx=0x%016lx rcx=0x%016lx rdx=0x%016lx " BOX_V "\n", regs->rax, regs->rbx, regs->rcx, regs->rdx);
    terminal_printf(WHITE, BLACK, BOX_V " rsi=0x%016lx rdi=0x%016lx rbp=0x%016lx                        " BOX_V "\n", regs->rsi, regs->rdi, regs->rbp);
    terminal_printf(WHITE, BLACK, BOX_V " r8 =0x%016lx r9 =0x%016lx r10=0x%016lx r11=0x%016lx " BOX_V "\n", regs->r8, regs->r9, regs->r10, regs->r11);
    terminal_printf(WHITE, BLACK, BOX_V " r12=0x%016lx r13=0x%016lx r14=0x%016lx r15=0x%016lx " BOX_V "\n", regs->r12, regs->r13, regs->r14, regs->r15);
    terminal_printf(WHITE, BLACK, BOX_V " rip=0x%016lx eflags=0x%016lx cs=0x%04lx                                  " BOX_V "\n", regs->rip, regs->eflags, regs->cs);
    terminal_printf(WHITE, BLACK, BOX_V " exception: 0x%02lx %-75s " BOX_V "\n", regs->int_no, "");
    terminal_printf(WHITE, BLACK, BOX_V " error_code: 0x%016lx                                                              " BOX_V "\n", regs->err_code);
    terminal_printf(WHITE, BLACK, BOX_BL BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_BR "\n\n");
    terminal_printf(WHITE, BLACK, "Thank you for using Kunix. It is now safe to restart your computer.\n");

    kserialout("KERNEL PANIC! ");
    kserialout(reason);

    while(1) { __asm__ volatile ("hlt"); }
}