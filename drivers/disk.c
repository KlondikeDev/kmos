// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// Disks by number (include/disk.h): ATA drives, and ramdisks -- images
// Limine loaded as modules, which is how Kunix boots a machine whose disks
// it has no driver for yet (the live image). A ramdisk is writable, but
// what is written lives only until the machine goes down.

#include <disk.h>
#include <ata.h>
#include <limine.h>
#include <memcpy.h>
#include <klog.h>
#include <klibc/string.h>

volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0,
};

typedef struct {
    u8* base;
    u64 sectors;
} ramdisk_t;

static ramdisk_t ramdisks[DISK_MAX - DISK_RAM0];
static u32 ramdisk_count;

static ramdisk_t* ram(u8 disk) {
    if (disk < DISK_RAM0 || disk >= DISK_RAM0 + ramdisk_count) return NULL;
    return &ramdisks[disk - DISK_RAM0];
}

bool disk_present(u8 disk) {
    if (disk < DISK_RAM0) return ata_present(disk);
    return ram(disk) != NULL;
}

bool disk_read(u8 disk, u64 lba, u32 count, void* buf) {
    if (disk < DISK_RAM0) {
        if (lba + count > 0xFFFFFFFULL) return false;          // LBA28
        return ata_read_multi(disk, (u32)lba, count, buf);
    }
    ramdisk_t* r = ram(disk);
    if (!r || lba > r->sectors || count > r->sectors - lba) return false;
    memcpy(buf, r->base + lba * 512, (u64)count * 512);
    return true;
}

bool disk_write(u8 disk, u64 lba, u32 count, const void* buf) {
    if (disk < DISK_RAM0) {
        if (lba + count > 0xFFFFFFFULL) return false;
        return ata_write_multi(disk, (u32)lba, count, buf);
    }
    ramdisk_t* r = ram(disk);
    if (!r || lba > r->sectors || count > r->sectors - lba) return false;
    memcpy(r->base + lba * 512, buf, (u64)count * 512);
    return true;
}

bool disk_flush(u8 disk) {
    if (disk < DISK_RAM0) return ata_flush(disk);
    return ram(disk) != NULL;
}

static bool ends_with(const char* s, const char* tail) {
    u64 n = strlen(s), t = strlen(tail);
    return n >= t && !strcmp(s + n - t, tail);
}

i32 ramdisk_from_module(const char* name) {
    struct limine_module_response* resp = module_request.response;
    if (!resp || ramdisk_count == DISK_MAX - DISK_RAM0) return -1;
    for (u64 i = 0; i < resp->module_count; i++) {
        struct limine_file* f = resp->modules[i];
        if (!f->path || !ends_with(f->path, name)) continue;
        ramdisk_t* r = &ramdisks[ramdisk_count];
        r->base = (u8*)f->address;
        r->sectors = f->size / 512;
        klog(LOG_OKAY, "disk: %s in memory as disk %u (%u MiB)\n", f->path,
             (u32)(DISK_RAM0 + ramdisk_count), (u32)(f->size >> 20));
        return (i32)(DISK_RAM0 + ramdisk_count++);
    }
    return -1;
}
