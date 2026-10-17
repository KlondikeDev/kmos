// This file has been generated in whole or in part by a Large Language Model

// Minimal fixed ACPI hardware support. Table layouts and register semantics:
// https://uefi.org/specs/ACPI/6.5/05_ACPI_Software_Programming_Model.html
// AML support is limited to a literal _S5_ package (acpi_aml.c).
#include <acpi.h>
#include <limine.h>
#include <memory.h>
#include <io.h>
#include <klog.h>
#include <memcpy.h>
#include <klibc/string.h>

static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST, .revision = 0
};

typedef struct PACKED {
    u8 space, width, offset, access;
    u64 address;
} gas_t;

static gas_t pm1a, pm1b, reset;
static volatile u8 *pm1a_mem, *pm1b_mem, *reset_mem;
static u16 type_a, type_b;
static u32 smi_command;
static u8 enable_command, reset_value;
static bool sleep_ready, reset_ready;

static u32 read32(const u8* p) { u32 v; memcpy(&v, p, sizeof(v)); return v; }
static u64 read64(const u8* p) { u64 v; memcpy(&v, p, sizeof(v)); return v; }

// Base revision 3 returns a physical RSDP. Reserved firmware pages need
// not be in Limine's direct map, so give tables and MMIO their own window.
static void* firmware_map(u64 physical, u64 size, bool device) {
    static u64 next = 0xffffc10000000000ULL;
    if (!physical || !size || size > 1024 * 1024 ||
        physical >= (1ULL << 52) || size > (1ULL << 52) - physical) return NULL;
    u64 base = physical & ~4095ULL, offset = physical & 4095;
    u64 length = (offset + size + 4095) & ~4095ULL;
    if (next + length > 0xffffc10001000000ULL) return NULL;
    u64 virt = next;
    next += length;
    for (u64 i = 0; i < length; i += 4096) {
        if (!vmm_map(virt + i, base + i, device ? 0x1b : 3)) return NULL;
    }
    return (void*)(virt + offset);
}

static bool checksum(const u8* bytes, u64 length) {
    u8 sum = 0;
    for (u64 i = 0; i < length; i++) sum += bytes[i];
    return sum == 0;
}

static const u8* table(u64 physical) {
    const u8* header = firmware_map(physical, 36, false);
    if (!header) return NULL;
    u32 length = read32(header + 4);
    if (length < 36 || length > 1024 * 1024) return NULL;
    const u8* full = firmware_map(physical, length, false);
    return full && checksum(full, length) ? full : NULL;
}

static bool prepare(gas_t* g, volatile u8** memory, u8 width) {
    if (!g->address || g->offset || g->width != width ||
        (g->access && g->access != (width == 16 ? 2 : 1))) return false;
    if (g->space == 1) return g->address <= 65536ULL - width / 8;
    if (g->space != 0 || (g->address & (width / 8 - 1))) return false;
    *memory = firmware_map(g->address, width / 8, true);
    return *memory != NULL;
}

static u16 control_read(const gas_t* g, volatile u8* memory) {
    return g->space == 1 ? inw((u16)g->address) : *(volatile u16*)memory;
}

static void control_write(const gas_t* g, volatile u8* memory, u16 value) {
    if (g->space == 1) outw((u16)g->address, value);
    else *(volatile u16*)memory = value;
}

static gas_t control_register(const u8* fadt, u32 length, u32 extended, u32 legacy) {
    gas_t g = {0};
    if (length >= extended + sizeof(g)) memcpy(&g, fadt + extended, sizeof(g));
    if (!g.address) {
        g.space = 1;
        g.width = 16;
        g.offset = 0;
        g.access = 2;
        g.address = read32(fadt + legacy);
    }
    return g;
}

void acpi_init(void) {
    if (!rsdp_request.response) { klog(LOG_WARN, "acpi: no RSDP\n"); return; }
    const u8* rsdp = firmware_map((u64)rsdp_request.response->address, 36, false);
    if (!rsdp || strncmp((const char*)rsdp, "RSD PTR ", 8) || !checksum(rsdp, 20)) {
        klog(LOG_WARN, "acpi: invalid RSDP\n"); return;
    }
    u64 root_phys = read32(rsdp + 16);
    u32 stride = 4;
    if (rsdp[15] >= 2) {
        u32 size = read32(rsdp + 20);
        const u8* extended = size >= 36 ? firmware_map((u64)rsdp_request.response->address, size, false) : NULL;
        if (extended && checksum(extended, size) && read64(extended + 24)) {
            root_phys = read64(extended + 24);
            stride = 8;
        }
    }
    const u8* root = table(root_phys);
    if (!root || strncmp((const char*)root, stride == 8 ? "XSDT" : "RSDT", 4)) {
        klog(LOG_WARN, "acpi: invalid root table\n"); return;
    }
    const u8* fadt = NULL;
    u32 entries = (read32(root + 4) - 36) / stride;
    if (entries > 512) entries = 512;
    for (u32 i = 0; i < entries; i++) {
        const u8* p = root + 36 + i * stride;
        const u8* candidate = table(stride == 8 ? read64(p) : read32(p));
        if (candidate && !strncmp((const char*)candidate, "FACP", 4)) { fadt = candidate; break; }
    }
    if (!fadt || read32(fadt + 4) < 116) {
        klog(LOG_WARN, "acpi: no valid FADT\n"); return;
    }
    u32 length = read32(fadt + 4), flags = read32(fadt + 112);
    if ((flags & (1u << 10)) && length >= 129) {
        memcpy(&reset, fadt + 116, sizeof(reset));
        reset_value = fadt[128];
        reset_ready = prepare(&reset, &reset_mem, 8);
    }
    if (!(flags & (1u << 20)) && fadt[89] >= 2) {
        pm1a = control_register(fadt, length, 172, 64);
        pm1b = control_register(fadt, length, 184, 68);
        smi_command = read32(fadt + 48);
        enable_command = fadt[52];
        u64 dsdt_phys = length >= 148 ? read64(fadt + 140) : 0;
        if (!dsdt_phys) dsdt_phys = read32(fadt + 40);
        const u8* dsdt = table(dsdt_phys);
        sleep_ready = prepare(&pm1a, &pm1a_mem, 16) &&
            (!pm1b.address || prepare(&pm1b, &pm1b_mem, 16)) &&
            dsdt && !strncmp((const char*)dsdt, "DSDT", 4) &&
            acpi_find_s5(dsdt + 36, read32(dsdt + 4) - 36, &type_a, &type_b);
    }
    klog(LOG_OKAY, "acpi: S5 %s, reset %s\n", sleep_ready ? "ready" : "unavailable",
         reset_ready ? "ready" : "unavailable");
}

bool acpi_poweroff(void) {
    if (!sleep_ready) return false;
    if (!(control_read(&pm1a, pm1a_mem) & 1)) {
        if (!smi_command || smi_command > 65535 || !enable_command) return false;
        outb((u16)smi_command, enable_command);
        // Firmware may need time to handle the SMI. Bound the wait so
        // broken firmware cannot prevent fallback shutdown or a halt.
        u32 tries = 1000000;
        while (!(control_read(&pm1a, pm1a_mem) & 1) && --tries) inb(0x80);
        if (!tries) return false;
    }
    u16 a = (control_read(&pm1a, pm1a_mem) & ~0x3c00u) | type_a;
    u16 b = pm1b.address ? (control_read(&pm1b, pm1b_mem) & ~0x3c00u) | type_b : 0;
    klog(LOG_OKAY, "acpi: entering S5\n");
    control_write(&pm1a, pm1a_mem, a);
    if (pm1b.address) control_write(&pm1b, pm1b_mem, b);
    control_write(&pm1a, pm1a_mem, a | (1u << 13));
    if (pm1b.address) control_write(&pm1b, pm1b_mem, b | (1u << 13));
    return true;
}

bool acpi_reset(void) {
    if (!reset_ready) return false;
    klog(LOG_OKAY, "acpi: issuing firmware reset\n");
    if (reset.space == 1) outb((u16)reset.address, reset_value);
    else *reset_mem = reset_value;
    return true;
}

u64 acpi_rsdp_physical(void) {
    return rsdp_request.response ? (u64)rsdp_request.response->address : 0;
}
