// This file has been generated in whole or in part by a Large Language Model
#pragma once
#include <types.h>
#define GPT_MAX_PARTITIONS 128
// GUID bytes retain their GPT on-disk order; helpers handle the mixed endian text.
typedef struct {
    u8 parent;
    u32 slot;                         // original one-based GPT entry number
    u64 start, count;                  // normalized to disk_read's 512-byte units
    u8 type[16], guid[16];
    u64 attributes;
    char name[37];                    // printable ASCII subset of the GPT name
} gpt_partition_t;
// Validates the entire table before copying any entries. Errors leave out intact.
// Supports up to 128 table slots and entry sizes 128 through 4096 bytes.
i32 gpt_scan(u8 disk, gpt_partition_t* out, u32 cap);
bool gpt_guid_parse(const char* text, u8 out[16]);
void gpt_guid_format(const u8 guid[16], char out[37]);
