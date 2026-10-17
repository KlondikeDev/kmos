// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once
#include <types.h>

bool identify_drive(uint8_t drive_select);
bool read_sectors(u32 lba, void* buffer);   // primary master
// Read one 512-byte sector. drive: 0 = primary master, 1 = primary slave.
bool ata_read(u8 drive, u32 lba, void* buffer);
bool ata_write(u8 drive, u32 lba, const void* buffer);
bool write_sectors(u32 lba, const void* buffer);  // primary master
void ata_print_model(void);

// Any of the four IDE drives: 0/1 primary master/slave, 2/3 secondary.
// count sectors of 512 bytes. Writes are not flushed: call ata_flush once
// the operation's writes are all out.
bool ata_read_multi(u8 drive, u32 lba, u32 count, void* buffer);
bool ata_write_multi(u8 drive, u32 lba, u32 count, const void* buffer);
bool ata_flush(u8 drive);
u64 ata_sector_count(u8 drive); // cached IDENTIFY LBA28 capacity; 0 if absent
bool ata_present(u8 drive);         // an ATA disk (not ATAPI) answers IDENTIFY

#define ATA_REG_DATA         0x1F0
#define ATA_REG_SECTOR_COUNT 0x1F2
#define ATA_REG_LBA_LOW      0x1F3
#define ATA_REG_LBA_MID      0x1F4
#define ATA_REG_LBA_HI       0x1F5
#define ATA_REG_DRIVE        0x1F6
#define ATA_REG_STATUS       0x1F7
#define ATA_REG_ALT_STATUS   0x3F6

#define ATA_STATUS_BUSY      0x80
#define ATA_STATUS_DRQ       0x08
#define ATA_STATUS_ERR       0x01

#define ATA_CMD_IDENTIFY     0xEC
#define ATA_CMD_WRITE        0x30
#define ATA_CMD_FLUSH        0xE7