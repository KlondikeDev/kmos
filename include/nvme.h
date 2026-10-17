// This file has been generated in whole or in part by a Large Language Model
#pragma once
#include <types.h>

// Namespace indices are stable after init. All addresses/counts use 512 bytes,
// including namespaces whose native block size is 4096 bytes.
void nvme_init(void);
u32 nvme_count(void);
u64 nvme_sectors(u8 index);
u32 nvme_block_size(u8 index);
bool nvme_read(u8 index, u64 lba, u32 count, void* buffer);
bool nvme_write(u8 index, u64 lba, u32 count, const void* buffer);
bool nvme_flush(u8 index);
