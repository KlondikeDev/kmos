// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <boxes.h>
#include <console.h>
#include <io.h>
#include <types.h>
#include <klibc/string.h>
#include <memcpy.h>

// CPUID functions:
#define CHK_MAX_LEAF        0x80000000
#define EXT_PROCESSOR_INFO  0x80000001
#define PROCESSOR_BRAND_1   0x80000002
#define PROCESSOR_BRAND_2   0x80000003
#define PROCESSOR_BRAND_3   0x80000004

bool extendedCPUID = true;
uint32_t max_extended_leaf = 0; 

// Runs CPUID instruction
void cpuid(uint32_t func, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx) {
    uint64_t rax, rbx, rcx, rdx;
    __asm__ volatile("cpuid"
        : "=a"(rax), "=b"(rbx), "=c"(rcx), "=d"(rdx)
        : "a"((uint64_t)func)
        : );
    *eax = (uint32_t)rax;
    *ebx = (uint32_t)rbx;
    *ecx = (uint32_t)rcx;
    *edx = (uint32_t)rdx;
}

// Checks the CPU vendor and stores it in the vendor array.
void cpuid_vendor(char* vendor) {
    uint32_t eax, ebx, ecx, edx;

    cpuid(0, &eax, &ebx, &ecx, &edx);

    *(uint32_t *)(vendor + 0) = ebx;
    *(uint32_t *)(vendor + 4) = edx;
    *(uint32_t *)(vendor + 8) = ecx;
    vendor[12] = '\0';
}

// Checks if Extended CPUID is enabled
void cpuid_extended(void) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(CHK_MAX_LEAF, &eax, &ebx, &ecx, &edx); // Checks the max CPUID leaf
    max_extended_leaf = eax; // Stores maxiumum exteneded CPUID leaf
    terminal_printf(SILVER, BLACK, BOX_V " Maximum extended CPUID leaf: 0x%x           " BOX_V "\n", max_extended_leaf);
    if(eax <= CHK_MAX_LEAF) {
        terminal_printf(SILVER, BLACK, "Extended CPUID not supported\n");
        extendedCPUID = false;
        return;
    } else {
        extendedCPUID = true;
    }

}

void cpuid_brand(char* brand) {
    uint32_t eax, ebx, ecx, edx;

    if (max_extended_leaf < PROCESSOR_BRAND_3) {
        return;
    }

    cpuid(PROCESSOR_BRAND_1, &eax, &ebx, &ecx, &edx);

    memcpy(brand + 0, &eax, 4);
    memcpy(brand + 4, &ebx, 4);
    memcpy(brand + 8, &ecx, 4);
    memcpy(brand + 12, &edx, 4);

    cpuid(PROCESSOR_BRAND_2, &eax, &ebx, &ecx, &edx);

    memcpy(brand + 16, &eax, 4);
    memcpy(brand + 20, &ebx, 4);
    memcpy(brand + 24, &ecx, 4);
    memcpy(brand + 28, &edx, 4);

    cpuid(PROCESSOR_BRAND_3, &eax, &ebx, &ecx, &edx);

    memcpy(brand + 32, &eax, 4);
    memcpy(brand + 36, &ebx, 4);
    memcpy(brand + 40, &ecx, 4);
    memcpy(brand + 44, &edx, 4);

    brand[47] = '\0';

}

void cpuid_sse(void) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
        if (edx & (1 << 25)){
            terminal_printf(SILVER, BLACK, BOX_V " SSE supported                                     " BOX_V "\n");
        } else {
            terminal_printf(SILVER, BLACK, BOX_V " SSE unsupported\n");
        }
        if (edx & (1 << 26)){
            terminal_printf(SILVER, BLACK, BOX_V " SSE2 supported                                    " BOX_V "\n");
        } else {
            terminal_printf(SILVER, BLACK, BOX_V " SSE2 unsupported");
        }
}

void cpuid_detect(void) {
    char vendor[13];
    char brand[49];
    cpuid_vendor(vendor);
    terminal_printf(SILVER, BLACK, BOX_TL " CPU Info: " BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_TR "\n");
    terminal_printf(SILVER, BLACK, BOX_V " CPU Vendor: %s                          " BOX_V "\n", vendor);
    cpuid_extended();
    if(extendedCPUID){
        cpuid_brand(brand);
        int len = strlen(brand) - 1;
        while (len >= 0 && brand[len] == ' ') {
            brand[len] = '\0';
            len--;
        }
        int actual_len = strlen(brand);
        for (int i = actual_len; i < 32; i++) {
            brand[i] = ' ';
        }
        brand[32] = '\0';
        terminal_printf(SILVER, BLACK, BOX_V " CPU Brand/Model: %s " BOX_V"\n", brand);
        cpuid_sse();
    }

    terminal_printf(SILVER, BLACK, BOX_BL BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_H BOX_BR "\n\n");
}