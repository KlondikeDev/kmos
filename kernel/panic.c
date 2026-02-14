#include <boxes.h>
#include <console.h>
#include <serial.h>
#include <idt.h>

// Yes, this is highly fragile. But, it works for now. It WILL be fixed.

void panic(const char* reason, registers_t* regs) {
    terminal_printf(WHITE, BLACK, "panic: %s. execution halted.\n\n", reason);
    terminal_printf(WHITE, BLACK, BOX_TL " registers: " BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_TR "\n");
    terminal_printf(WHITE, BLACK, BOX_V " eax=0x%08x ebx=0x%08x ecx=0x%08x edx=0x%08x " BOX_V "\n", regs->eax, regs->ebx, regs->ecx, regs->edx);
    terminal_printf(WHITE, BLACK, BOX_V " esi=0x%08x edi=0x%08x ebp=0x%08x esp=0x%08x " BOX_V "\n", regs->esi, regs->edi, regs->ebp, regs->esp);
    terminal_printf(WHITE, BLACK, BOX_V " eip=0x%08x eflags=0x%08x                            " BOX_V "\n", regs->eip, regs->eflags);
    terminal_printf(WHITE, BLACK, BOX_V " cs=0x%08x ds=0x%08x                                 " BOX_V "\n", regs->cs, regs->ds);
    terminal_printf(WHITE, BLACK, BOX_V " exception: 0x%08x                                       " BOX_V "\n", regs->int_no);
    terminal_printf(WHITE, BLACK, BOX_V " error_code: 0x%08x                                      " BOX_V "\n", regs->err_code);
    terminal_printf(WHITE, BLACK, BOX_BL BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_BR "\n\n");
    terminal_printf(WHITE, BLACK, "Thank you for using Kunix. It is now safe to restart your computer.\n");

    kserialout("KERNEL PANIC! ");
    kserialout(reason);

    while(1) { __asm__ volatile ("hlt"); }
}