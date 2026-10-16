#pragma once
#include <stdint.h>
#define FS_BSDFFS 7
#define FSMAXTYPES 32
static const char* const fstypenames[FSMAXTYPES] = { "unused" };
#define DIOCGDINFO 0x40000000
struct partition { uint32_t p_size, p_offset, p_fsize; uint8_t p_fstype, p_frag; uint16_t p_cpg; };
struct disklabel { uint32_t d_secsize, d_nsectors, d_ntracks, d_secpercyl, d_npartitions; uint32_t d_bbsize, d_sbsize; struct partition d_partitions[8]; };
