#define KUNIX_BUILD_DATE __DATE__
#define KUNIX_BUILD_TIME __TIME__
#define KUNIX_NAME     "kunix"
#define KUNIX_VERSION  "0.01"
#define KUNIX_CODENAME "kodiak"
#define KUNIX_ARCH     "i386"

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
    klog(LOG_OKAY, "Booting Kunix Millennium Operating System...\n");
    klog(LOG_DEBUG, "Kernel Built at %s on %s: %s-%s (%s)\n", KUNIX_BUILD_TIME, KUNIX_BUILD_DATE, KUNIX_NAME, KUNIX_CODENAME, KUNIX_ARCH);
    idt_init();
	pit_init();
    klog(LOG_OKAY, "Programmable Interval Timer (PIT) set to 250hz (Square Wave)!\n");
	init_serial();
    pci_list_devices();
    terminal_printf(BLACK, BLACK, "\n");
	cpuid_detect();
	terminal_printf(CYAN, BLACK, "Upper RAM: %uKB\n", mbi->mem_upper);
	terminal_printf(CYAN, BLACK, "Lower RAM: %uKB\n\n", mbi->mem_lower);

	terminal_printf(WHITE, BLACK, "kunix-kodiak v0.01 - Towards a New Digital Millennium!\n");
	while(1) { __asm__ volatile ("hlt"); }
}

// Happy 15th to me! 12:00 AM!