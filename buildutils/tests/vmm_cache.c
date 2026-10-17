// This file has been generated in whole or in part by a Large Language Model
// Synthetic page tables exercise the real walker without privileged instructions.
#include <stdio.h>
#include "../../kernel/memory.c"
static u64 tables[4][512] __attribute__((aligned(4096)));
int main(void) {
    tables[0][0] = (u64)tables[1] | 3;
    tables[1][0] = (u64)tables[2] | 3;
    tables[2][0] = (u64)tables[3] | 3;
    tables[3][0] = 0x400000 | 0x8b; // PAT[5]: PWT + PAT
    if (vmm_cache_flags((u64)tables[0], 0) != 0x88) return 1;
    tables[2][0] = 0x200000 | 0x108b; // 2 MiB leaf: PAT is bit 12
    if (vmm_cache_flags((u64)tables[0], 0) != 0x88) return 2;
    tables[1][0] = 0x40000000 | 0x109b; // 1 GiB leaf: PAT[7]
    if (vmm_cache_flags((u64)tables[0], 0) != 0x98) return 3;
    tables[1][0] = 0;
    if (vmm_cache_flags((u64)tables[0], 0) != 0) return 4;
    puts("vmm cache flags: passed");
    return 0;
}
