# Copyright (c) 2026 Joseph Jones
# Licensed under the Kunix License v1.0 or later
# See LICENSE for details

.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set VIDEO,    1<<2
.set FLAGS,    ALIGN | MEMINFO | VIDEO
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)
.set KERNEL_VIRTUAL_BASE, 0xFFFFFFFF80000000

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

.section .boot.bss, "aw", @nobits
.align 4096
boot_pml4:
    .skip 4096
boot_pdpt_low:
    .skip 4096
boot_pdpt_high:
    .skip 4096
boot_pd:
    .skip 4096
boot_pd_high:
    .skip 4096
boot_pd_mmio:
    .skip 4096

.align 16
boot_stack_bottom:
.skip 65536
boot_stack_top:

.section .boot.data, "aw", @progbits
.align 16
boot_gdt64:
    .quad 0
    .quad 0x00AF9A000000FFFF
    .quad 0x00AF92000000FFFF
    .quad 0x00AFFA000000FFFF
    .quad 0x00AFF2000000FFFF
boot_gdt64_ptr:
    .word boot_gdt64_ptr - boot_gdt64 - 1
    .quad boot_gdt64

.section .bss
.align 16
stack_bottom:
    .skip 65536
stack_top:

.section .text._start, "ax"
.code32
.global _start
.type _start, @function
_start:
    mov $boot_stack_top, %esp
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

    # Clear page tables (6 tables now)
    mov $boot_pml4, %edi
    xor %eax, %eax
    mov $6144, %ecx
    rep stosl

    # PML4[0] → boot_pdpt_low (identity)
    mov $boot_pdpt_low, %eax
    or $3, %eax
    mov %eax, boot_pml4

    # PML4[511] → boot_pdpt_high (higher-half -2GB)
    mov $boot_pdpt_high, %eax
    or $3, %eax
    mov %eax, boot_pml4 + (511 * 8)

    # PDPT_LOW[0] → boot_pd (0–1GB, kernel + identity)
    mov $boot_pd, %eax
    or $3, %eax
    mov %eax, boot_pdpt_low

    # PDPT_LOW[2] → boot_pd_mmio (0x80000000–0xBFFFFFFF)
    mov $boot_pd_mmio, %eax
    or $3, %eax
    mov %eax, boot_pdpt_low + (2 * 8)

    # PDPT_LOW[3] → boot_pd_high (0xC0000000–0xFFFFFFFF)
    mov $boot_pd_high, %eax
    or $3, %eax
    mov %eax, boot_pdpt_low + (3 * 8)

    # PDPT_HIGH[510] → boot_pd (kernel at -2GB)
    mov $boot_pd, %eax
    or $3, %eax
    mov %eax, boot_pdpt_high + (510 * 8)

    # PDPT_HIGH[511] → boot_pd_high (high-half MMIO mirror)
    mov $boot_pd_high, %eax
    or $3, %eax
    mov %eax, boot_pdpt_high + (511 * 8)

    # Identity map first 64MB (0x0–0x3FFFFFF), write-back
    mov $0x83, %eax
    mov $boot_pd, %edi
    mov $32, %ecx
1:
    mov %eax, (%edi)
    add $0x200000, %eax
    add $8, %edi
    loop 1b

    # Map 0x80000000–0xBFFFFFFF (boot_pd_mmio, 512 entries, uncached)
    mov $0x8000009B, %eax
    mov $boot_pd_mmio, %edi
    mov $512, %ecx
2:
    mov %eax, (%edi)
    add $0x200000, %eax
    add $8, %edi
    loop 2b

    # Map 0xC0000000–0xFFFFFFFF (boot_pd_high, 512 entries, uncached)
    mov $0xC000009B, %eax
    mov $boot_pd_high, %edi
    mov $512, %ecx
3:
    mov %eax, (%edi)
    add $0x200000, %eax
    add $8, %edi
    loop 3b

    # Enable paging
    mov $boot_pml4, %eax
    mov %eax, %cr3

    mov %cr4, %eax
    or $(1 << 5), %eax
    mov %eax, %cr4

    mov $0xC0000080, %ecx
    rdmsr
    or $(1 << 8), %eax
    wrmsr

    mov %cr0, %eax
    or $(1 << 31), %eax
    mov %eax, %cr0

    lgdt boot_gdt64_ptr

    pushl $0x08
    pushl $start64
    lret

no_long_mode:
    cli
    hlt
    jmp no_long_mode

.code64
start64:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    xor %ax, %ax
    mov %ax, %fs
    mov %ax, %gs

    movabs $higher_half, %rax
    jmp *%rax

higher_half:
    movabs $stack_top, %rsp
    and $0xFFFFFFFFFFFFFFF0, %rsp

    mov %esi, %edi

    call kernel_main

    cli
4:
    hlt
    jmp 4b
    