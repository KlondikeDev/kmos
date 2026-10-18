// This file has been generated in whole or in part by a Large Language Model
#pragma once
// Shared kernel service ABI; usable by native Kunix and compatibility clients.
#define KDISK_INFO 0x4b4401
#define KDISK_RESCAN 0x4b4402
#define KDISK_WHOLE 1u
#define KDISK_MOUNTED 2u
#define KDISK_RAM 4u
#define KDISK_READONLY 8u
#define KDISK_NVME 16u
#define KDISK_ESP 32u
#define KDISK_BLKGETSIZE64 0x80081272u
#define KDISK_BLKSSZGET 0x1268u
struct kdisk_info {
    unsigned long long sectors;
    unsigned int block_size, flags, parent, slot;
    unsigned char guid[16];
};
