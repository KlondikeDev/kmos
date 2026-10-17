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
#define DISK_RAM0 4
#define DISK_MAX  8

bool disk_present(u8 disk);
bool disk_read(u8 disk, u64 lba, u32 count, void* buf);       // 512-byte sectors
bool disk_write(u8 disk, u64 lba, u32 count, const void* buf);
bool disk_flush(u8 disk);

// A bootloader module whose path ends in `name`, as a ramdisk: its disk
// number, or -1 if there is no such module.
i32 ramdisk_from_module(const char* name);
