// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// This file has been generated in whole or in part by a Large Language Model

#pragma once
#include <types.h>
#include <vfs.h>

// FAT32 BIOS Parameter Block (boot sector / LBA 0).
// Offsets follow the Microsoft FAT spec exactly; the struct MUST be packed
// so member offsets line up byte-for-byte with the on-disk layout. Read
// sector 0 into a uint8_t[512] and cast that buffer to a bpb_t*.
typedef struct PACKED {
    u8  jmp_boot[3];        // 0   BS_jmpBoot
    u8  oem_name[8];        // 3   BS_OEMName
    u16 bytes_per_sector;   // 11  BPB_BytsPerSec
    u8  sectors_per_cluster;// 13  BPB_SecPerClus
    u16 reserved_sectors;   // 14  BPB_RsvdSecCnt
    u8  num_fats;           // 16  BPB_NumFATs
    u16 root_entry_count;   // 17  BPB_RootEntCnt   (0 on FAT32)
    u16 total_sectors_16;   // 19  BPB_TotSec16     (0 on FAT32)
    u8  media;              // 21  BPB_Media
    u16 fat_size_16;        // 22  BPB_FATSz16      (0 on FAT32)
    u16 sectors_per_track;  // 24  BPB_SecPerTrk
    u16 num_heads;          // 26  BPB_NumHeads
    u32 hidden_sectors;     // 28  BPB_HiddSec
    u32 total_sectors_32;   // 32  BPB_TotSec32
    // FAT32-specific fields begin at offset 36
    u32 fat_size_32;        // 36  BPB_FATSz32      (sectors per FAT)
    u16 ext_flags;          // 40  BPB_ExtFlags
    u16 fs_version;         // 42  BPB_FSVer
    u32 root_cluster;       // 44  BPB_RootClus     (usually 2)
    u16 fs_info;            // 48  BPB_FSInfo
    u16 backup_boot_sector; // 50  BPB_BkBootSec
    u8  reserved[12];       // 52  BPB_Reserved
    u8  drive_number;       // 64  BS_DrvNum
    u8  reserved1;          // 65  BS_Reserved1
    u8  boot_signature;     // 66  BS_BootSig
    u32 volume_id;          // 67  BS_VolID
    u8  volume_label[11];   // 71  BS_VolLab
    u8  fs_type[8];         // 82  BS_FilSysType
    u8  boot_code[420];     // 90  boot code
    u16 signature;          // 510 0xAA55
} bpb_t;                    // total: 512 bytes

// FAT32 8.3 directory entry (32 bytes on disk).
typedef struct PACKED {
    u8  name[11];           // 0   8.3 name, space-padded, no dot
    u8  attr;               // 11  attribute flags
    u8  nt_reserved;        // 12
    u8  create_time_tenth;  // 13
    u16 create_time;        // 14
    u16 create_date;        // 16
    u16 access_date;        // 18
    u16 cluster_hi;         // 20  high 16 bits of first cluster
    u16 write_time;         // 22
    u16 write_date;         // 24
    u16 cluster_lo;         // 26  low 16 bits of first cluster
    u32 size;               // 28  file size in bytes
} dirent_t;                 // total: 32 bytes

// Directory-entry attribute bits and sentinels
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LONG_NAME  0x0F   // long-file-name entry: skip these

#define FAT_DIRENT_END      0x00   // name[0]: no more entries
#define FAT_DIRENT_DELETED  0xE5   // name[0]: deleted entry

#define FAT_CLUSTER_EOC     0x0FFFFFF8 // chain end: entry >= this value
#define FAT_ENTRY_MASK      0x0FFFFFFF // FAT32 entries are 28 bits

// ---- Phase 1: mount ----
bool fat32_init(void);

// ---- Phase 2: cluster math + FAT walk ----
u32 cluster_to_lba(u32 cluster);
u32 fat_next_cluster(u32 cluster);

// ---- Phase 3/4: directory + file ----
void fat32_list(void);                  // list the working directory
bool fat32_chdir(const char* path);     // change working directory
const char* fat32_pwd(void);            // current working-directory path
// Returns bytes read (>= 0) on success, or -1 if the file isn't found.
i32 fat32_read_file(const char* path, void* buffer, u32 max_bytes);

extern const vfs_ops_t fat32_vfs_ops;
