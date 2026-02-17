ENTRY(_start)

SECTIONS
{
    /* Boot section at physical 2M */
    . = 0x200000;
    
    .boot ALIGN(4K) : {
        *(.multiboot)
        *(.text._start)
    }
    
    .boot.bss ALIGN(4K) : {
        *(.boot.bss)
    }
    
    .boot.data ALIGN(4K) : {
        *(.boot.data)
    }
    
    /* Kernel at higher-half */
    . += 0xFFFFFFFF80000000;
    
    .text ALIGN(4K) : AT(ADDR(.text) - 0xFFFFFFFF80000000) {
        *(.text)
    }

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - 0xFFFFFFFF80000000) {
        *(.rodata)
        *(.rodata.*)
    }

    .data ALIGN(4K) : AT(ADDR(.data) - 0xFFFFFFFF80000000) {
        *(.data)
        *(.data.*)
    }

    .bss ALIGN(4K) : AT(ADDR(.bss) - 0xFFFFFFFF80000000) {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }
}