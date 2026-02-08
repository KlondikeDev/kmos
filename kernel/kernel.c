#include <types.h>
#include <io.h>
#include <idt.h>
#include <serial.h>
#include <pit.h>
#include <keyboard.h>
#include <console.h>
#include <panic.h>

volatile int read_index = 0; 

#define MULTIBOOT_FLAG_FRAMEBUFFER (1 << 12)

// Kernel Entry point!
void kernel_main(multiboot_info_t* mbi) {
	if (!(mbi->flags & MULTIBOOT_FLAG_FRAMEBUFFER)) {
		kserialout("FAILED TO RECEIVE FRAMEBUFFER FROM GNU GRUB. Halt.\n");
		while(1) { __asm__ volatile ("hlt"); }
	}
	framebuffer_init(mbi);
	terminal_init();
	init_serial();
	terminal_printf(WHITE, BLACK, "Booting Kunix Millennium Operating System...\n");
	terminal_printf(CYAN, BLACK, "Upper RAM: %uKB\n", mbi->mem_upper);
	terminal_printf(CYAN, BLACK, "Lower RAM: %uKB\n", mbi->mem_lower);
	idt_init();
	pit_init();
	terminal_printf(CYAN, BLACK, "Programmable Interval Timer (PIT) set to 250hz (Square Wave)!\n");
	while(1) { __asm__ volatile ("hlt"); }
}