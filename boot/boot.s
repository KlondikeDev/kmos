.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set VIDEO,    1<<2
.set FLAGS,    ALIGN | MEMINFO | VIDEO
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM
.long 0, 0, 0, 0, 0
.long 0  # mode_type
.long 0  # width
.long 0  # height
.long 32 # depth

.section .bss
.align 4096
boot_pml4:
    .skip 4096
boot_pdpt:
    .skip 4096
boot_pd:
    .skip 4096
boot_pd_high:
    .skip 4096

.align 16
stack_bottom:
.skip 65536
stack_top:

.section .data
.align 16
gdt64:
    .quad 0                                          # Null descriptor
    .quad 0x00AF9A000000FFFF                        # 64-bit code (ring 0)
    .quad 0x00AF92000000FFFF                        # 64-bit data (ring 0)
    .quad 0x00AFFA000000FFFF                        # 64-bit code (ring 3)
    .quad 0x00AFF2000000FFFF                        # 64-bit data (ring 3)
gdt64_ptr:
    .word gdt64_ptr - gdt64 - 1
    .quad gdt64

.section .text
.code32
.global _start
.type _start, @function
_start:
    mov $stack_top, %esp
    
    # Save multiboot pointer to ESI (won't be clobbered)
    mov %ebx, %esi
    
    # Check CPUID support
    pushfl
    pop %eax
    mov %eax, %ecx
    xor $(1 << 21), %eax
    push %eax
    popfl
    pushfl
    pop %eax
    push %ecx
    popfl
    cmp %eax, %ecx
    je no_long_mode
    
    # Check long mode
    mov $0x80000000, %eax
    cpuid
    cmp $0x80000001, %eax
    jb no_long_mode
    
    mov $0x80000001, %eax
    cpuid
    test $(1 << 29), %edx
    jz no_long_mode
    
    # Clear page tables (4 tables × 4096 bytes = 4096 dwords)
    mov $boot_pml4, %edi
    mov $0, %eax
    mov $4096, %ecx
    rep stosl
    
    # Build page tables
    mov $boot_pdpt, %eax
    or $3, %eax
    mov %eax, boot_pml4
    
    # PDPT[0] → boot_pd (0-1GB)
    mov $boot_pd, %eax
    or $3, %eax
    mov %eax, boot_pdpt
    
    # PDPT[3] → boot_pd_high (3-4GB) - SEPARATE TABLE
    mov $boot_pd_high, %eax
    or $3, %eax
    mov %eax, boot_pdpt + (3 * 8)
    
    # Identity map first 64MB (kernel + stack)
    mov $0x83, %eax
    mov $32, %ecx                    # 32 entries × 2MB = 64MB
    mov $boot_pd, %edi
fill_pd_low:
    mov %eax, (%edi)
    add $0x200000, %eax
    add $8, %edi
    loop fill_pd_low
    
    # Identity map 0xE0000000-0xFFFFFFFF (512MB) in boot_pd_high
    mov $0xE0000083, %eax
    mov $boot_pd_high + (256 * 8), %edi    # Entry 256
    mov $256, %ecx                          # 256 entries = 512MB
fill_pd_high:
    mov %eax, (%edi)
    add $0x200000, %eax
    add $8, %edi
    loop fill_pd_high
    
    # Load page tables
    mov $boot_pml4, %eax
    mov %eax, %cr3
    
    # Enable PAE
    mov %cr4, %eax
    or $(1 << 5), %eax
    mov %eax, %cr4
    
    # Set LME bit
    mov $0xC0000080, %ecx
    rdmsr
    or $(1 << 8), %eax
    wrmsr
    
    # Enable paging
    mov %cr0, %eax
    or $(1 << 31), %eax
    mov %eax, %cr0
    
    # Load 64-bit GDT
    lgdt gdt64_ptr
    
    # Far jump to 64-bit code
    pushl $0x08
    pushl $start64
    lret

no_long_mode:
    cli
    hlt
    jmp no_long_mode

.code64
start64:
    mov $stack_top, %rsp

    # Set data segments to 0x10 (NOT 0!)
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    xor %ax, %ax
    mov %ax, %fs
    mov %ax, %gs
    
    # Align stack
    and $0xFFFFFFFFFFFFFFF0, %rsp
    
    # Pass multiboot pointer (saved in esi) to kernel_main
    mov %esi, %edi
    
    call kernel_main

    cli
hang:
    hlt
    jmp hang
	