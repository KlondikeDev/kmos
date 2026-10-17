// This file has been generated in whole or in part by a Large Language Model

#include <acpi.h>

// This deliberately handles literal S5 packages only. Evaluating methods
// requires an AML interpreter; encountering one must leave poweroff to
// the fallback rather than guess a sleep type.
static bool integer(const u8** pos, const u8* end, u16* out) {
    const u8* p = *pos;
    if (p == end) return false;
    u8 op = *p++;
    u64 value = 0;
    if (op == 0 || op == 1) value = op;
    else {
        u32 n = op == 0x0a ? 1 : op == 0x0b ? 2 : op == 0x0c ? 4 : op == 0x0e ? 8 : 0;
        if (!n || (u64)(end - p) < n) return false;
        for (u32 i = 0; i < n; i++) value |= (u64)*p++ << (8 * i);
    }
    if (value > 7) return false;
    *out = (u16)value << 10;
    *pos = p;
    return true;
}

bool acpi_find_s5(const u8* aml, u64 size, u16* a, u16* b) {
    const u8* end = aml + size;
    for (u64 i = 0; i + 7 <= size; i++) {
        const u8* p = aml + i;
        if (*p++ != 0x08) continue;             // NameOp
        if (*p == '\\') p++;                  // optional root prefix
        if ((u64)(end - p) < 6 || p[0] != '_' || p[1] != 'S' || p[2] != '5' || p[3] != '_') continue;
        p += 4;
        if (*p++ != 0x12) continue;             // PackageOp
        const u8* start = p;
        u8 lead = *p++;
        u32 follow = lead >> 6;
        if ((u64)(end - p) < follow) continue;
        if (follow && (lead & 0x30)) continue; // reserved bits
        u64 length = lead & (follow ? 0x0f : 0x3f);
        for (u32 j = 0; j < follow; j++) length |= (u64)*p++ << (4 + 8 * j);
        if (length > (u64)(end - start) || length <= (u64)(p - start)) continue;
        const u8* package_end = start + length;
        if (*p++ < 2) continue;
        u16 va, vb;
        if (!integer(&p, package_end, &va) || !integer(&p, package_end, &vb)) continue;
        *a = va;
        *b = vb;
        return true;
    }
    return false;
}
