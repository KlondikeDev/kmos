#include <types.h>
#include <io.h>
#include <console.h>
#include <klibc/string.h>

#define BOX_TL "\xDA"  // top-left
#define BOX_H  "\xC4"  // horizontal
#define BOX_TR "\xBF"  // top-right
#define BOX_V  "\xB3"  // vertical
#define BOX_BL "\xC0"  // bottom-left
#define BOX_BR "\xD9"  // bottom-right

bool extendedCPUID = true;
uint32_t eax, ebx, ecx, edx;

void cpuid(uint32_t func, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx) {
    __asm__ volatile("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(func));
}

void cpuid_vendor(char* vendor) {

    cpuid(0, &eax, &ebx, &ecx, &edx);

    *(uint32_t *)(vendor + 0) = ebx;
    *(uint32_t *)(vendor + 4) = edx;
    *(uint32_t *)(vendor + 8) = ecx;
    vendor[12] = '\0';
}

void cpuid_extended(void) {
    cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    terminal_printf(SILVER, BLACK, BOX_V " Maximum extended CPUID leaf: %x           " BOX_V "\n", eax);
    if(eax <= 0x80000000) {
        terminal_printf(SILVER, BLACK, "Extended CPUID not supported\n");
        extendedCPUID = false;
        return;
    } else {
        extendedCPUID = true;
    }

}

void cpuid_brand(char* brand) {
    if (eax < 0x80000004) {
        return;
    }

    cpuid(0x80000002, &eax, &ebx, &ecx, &edx);

    *(uint32_t *)(brand + 0) = eax;
    *(uint32_t *)(brand + 4) = ebx;
    *(uint32_t *)(brand + 8) = ecx;
    *(uint32_t *)(brand + 12) = edx;

    cpuid(0x80000003, &eax, &ebx, &ecx, &edx);

    *(uint32_t *)(brand + 16) = eax;
    *(uint32_t *)(brand + 20) = ebx;
    *(uint32_t *)(brand + 24) = ecx;
    *(uint32_t *)(brand + 28) = edx;

    cpuid(0x80000004, &eax, &ebx, &ecx, &edx);

    *(uint32_t *)(brand + 32) = eax;
    *(uint32_t *)(brand + 36) = ebx;
    *(uint32_t *)(brand + 40) = ecx;
    *(uint32_t *)(brand + 44) = edx;

    brand[47] = '\0';

}

void cpuid_sse(void) {
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