#include <console.h>
#include <serial.h>
#include <idt.h>

#define BOX_TL "\xDA"  // top-left
#define BOX_H  "\xC4"  // horizontal
#define BOX_TR "\xBF"  // top-right
#define BOX_V  "\xB3"  // vertical
#define BOX_BL "\xC0"  // bottom-left
#define BOX_BR "\xD9"  // bottom-right

void panic(const char* reason, registers_t* regs) {
    terminal_printf(WHITE, BLACK, "panic: %s. execution halted.\n\n", reason);
    terminal_printf(WHITE, BLACK, BOX_TL " registers: " BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_TR "\n");
    terminal_printf(WHITE, BLACK, BOX_V " eax=%x ebx=%x ecx=%x edx=%x " BOX_V "\n", regs->eax, regs->ebx, regs->ecx, regs->edx);
    terminal_printf(WHITE, BLACK, BOX_V " esi=%x edi=%x ebp=%x esp=%x " BOX_V "\n", regs->esi, regs->edi, regs->ebp, regs->esp);
    terminal_printf(WHITE, BLACK, BOX_V " eip=%x eflags=%x                            " BOX_V "\n", regs->eip, regs->eflags);
    terminal_printf(WHITE, BLACK, BOX_V " cs=%x ds=%x                                 " BOX_V "\n", regs->cs, regs->ds);
    terminal_printf(WHITE, BLACK, BOX_V " exception: %x                                       " BOX_V "\n", regs->int_no);
    terminal_printf(WHITE, BLACK, BOX_V " error_code: %x                                      " BOX_V "\n", regs->err_code);
    terminal_printf(WHITE, BLACK, BOX_BL BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_BR "\n\n");
    terminal_printf(WHITE, BLACK, "Thank you for using Kunix. It is now safe to restart your computer.\n");

    kserialout("KERNEL PANIC! ");
    kserialout(reason);

    while(1) { __asm__ volatile ("hlt"); }
}