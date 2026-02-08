.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set VIDEO,    1<<2
.set FLAGS,    ALIGN | MEMINFO | VIDEO  # No bit 16
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC              # Offset 0
.long FLAGS              # Offset 4
.long CHECKSUM           # Offset 8
.long 0                  # Offset 12 - header_addr (ignored for ELF)
.long 0                  # Offset 16 - load_addr (ignored)
.long 0                  # Offset 20 - load_end_addr (ignored)
.long 0                  # Offset 24 - bss_end_addr (ignored)
.long 0                  # Offset 28 - entry_addr (ignored)
.long 0                  # Offset 32 - mode_type (0 = linear fb)
.long 0	                 # Offset 36 - width
.long 0                  # Offset 40 - height
.long 32                 # Offset 44 - depth
.section .bss
.align 16
stack_bottom:
.skip 16384 # 16 KiB
stack_top:

/*
The linker script specifies _start as the entry point to the kernel and the
bootloader will jump to this position once the kernel has been loaded. It
doesn't make sense to return from this function as the bootloader is gone.
*/
.section .text
.global _start
.type _start, @function
_start:

	/*
	To set up a stack, we set the esp register to point to the top of the
	stack (as it grows downwards on x86 systems). This is necessarily done
	in assembly as languages such as C cannot function without a stack.
	*/
	mov $stack_top, %esp

	/*
	This is a good place to initialize crucial processor state before the
	high-level kernel is entered. It's best to minimize the early
	environment where crucial features are offline. Note that the
	processor is not fully initialized yet: Features such as floating
	point instructions and instruction set extensions are not initialized
	yet. The GDT should be loaded here. Paging should be enabled here.
	C++ features such as global constructors and exceptions will require
	runtime support to work as well.
	*/

	# Pass multiboot info pointer (in EBX) to kernel_main
	push %ebx
	call kernel_main

	cli
1:	hlt
	jmp 1b
.size _start, . - _start
