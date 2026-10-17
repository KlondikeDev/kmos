// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once
#include <types.h>

// Disks by number, whatever drives them: what filesystems read and write.
//
//   0-3   ATA (primary master/slave, secondary master/slave)
//   4-7   ramdisks: images the bootloader loaded into memory
//   8-15  NVMe namespaces; 16-127 GPT partitions
#define DISK_RAM0 4
#define DISK_MAX  128

// Initialize once after PMM/heap; a low-memory machine can run uncached.
void disk_cache_init(void);
// Deferred maintenance only: call from idle/syscall context, never a timer IRQ.
void disk_cache_poll(void);
typedef struct {
    u64 hits, misses, writebacks, errors;
    u32 blocks, dirty;
} disk_cache_stats_t;
void disk_cache_stats(disk_cache_stats_t* out);
u64 disk_sectors(u8 disk);
bool disk_present(u8 disk);
bool disk_read(u8 disk, u64 lba, u32 count, void* buf);       // 512-byte sectors
bool disk_write(u8 disk, u64 lba, u32 count, const void* buf);
bool disk_flush(u8 disk);

// A bootloader module whose path ends in `name`, as a ramdisk: its disk
// number, or -1 if there is no such module.
i32 ramdisk_from_module(const char* name);

#define DISK_NVME0 8
#define DISK_PART0 16
#define DISK_WHOLE_MAX 16
#define DISK_RAM_COUNT 4
u32 disk_block_size(u8 disk);
u32 disk_generation(u8 disk);
i32 disk_rescan(u8 whole);
void disk_scan_partitions(void);
i32 disk_find_uuid(const u8 guid[16]);
void disk_set_mounted(u8 disk, bool mounted);
bool disk_in_use(u8 disk);
u8 disk_parent(u8 disk);
u32 disk_partition_slot(u8 disk);
bool disk_partition_guid(u8 disk, u8 out[16]);
void disk_set_install_source(i32 disk);
i32 disk_install_source(void);
bool ramdisk_original_read(u8 disk, u64 lba, u32 count, void* out);

bool disk_overlaps(u8 a,u8 b);
