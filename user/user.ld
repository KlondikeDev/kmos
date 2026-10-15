/* This file has been generated in whole or in part by a Large Language Model */

/* Copyright (c) 2026 Joseph Jones
   Licensed under the Kunix License v1.0 or later
   See LICENSE for details */

/* Link script for ring 3 Kunix programs.

   Output is a static ELF64 executable, loaded by kernel/elf.c. The kernel
   maps each PT_LOAD segment with the permissions in its header, so the
   sections are grouped into three segments -- code (r-x), constants (r--)
   and writable data (rw-) -- each starting on its own page so no page ever
   needs two sets of permissions. */

ENTRY(_start)

PHDRS {
    text   PT_LOAD FLAGS(5);     /* r-x */
    rodata PT_LOAD FLAGS(4);     /* r-- */
    data   PT_LOAD FLAGS(6);     /* rw- */
}

SECTIONS {
    . = 0x400000;                /* USER_CODE_BASE in include/usermode.h */

    .text : { *(.text*) } :text

    . = ALIGN(4096);
    .rodata : { *(.rodata*) } :rodata

    . = ALIGN(4096);
    .data : { *(.data*) } :data
    .bss  : { *(.bss*) *(COMMON) } :data

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
