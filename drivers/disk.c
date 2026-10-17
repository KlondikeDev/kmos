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
#include <memory.h>
#include <ktime.h>
#include <io.h>

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

// One thirty-second of free RAM, capped at 32 MiB. Metadata adds less than
// two percent. A hash table finds blocks; the doubly linked list is the LRU.
#define CACHE_SECTORS 8
#define CACHE_MAX_BLOCKS 8192
#define CACHE_FLUSH_NS 5000000000ULL
#define CACHE_POLL_BLOCKS 32

typedef struct cache_block {
    struct cache_block *hash_next, *older, *newer;
    u64 lba;
    u8 disk, sectors;
    bool valid, dirty;
    u8 data[4096];
} cache_block_t;
static cache_block_t *blocks, **buckets, *oldest, *newest;
static u32 block_count, bucket_count, used_blocks;
static disk_cache_stats_t cache_stats;
static u64 next_flush;
static bool initialized;

void disk_cache_init(void) {
    if (initialized) return;
    initialized = true;
    u64 n = pmm_count_free() / 32;
    if (n > CACHE_MAX_BLOCKS) n = CACHE_MAX_BLOCKS;
    while (n) {
        blocks = kmalloc(n * sizeof(*blocks));
        buckets = kmalloc(n * 2 * sizeof(*buckets));
        if (blocks && buckets) break;
        kfree(blocks);
        kfree(buckets);
        blocks = NULL;
        buckets = NULL;
        n /= 2;
    }
    block_count = (u32)n;
    bucket_count = block_count * 2;
    for (u32 i = 0; i < bucket_count; i++) buckets[i] = NULL;
    for (u32 i = 0; i < block_count; i++) {
        blocks[i].valid = false;
        blocks[i].dirty = false;
    }
    next_flush = time_boot_ns() + CACHE_FLUSH_NS;
}

u64 disk_sectors(u8 disk) {
    if (disk < DISK_RAM0) return ata_sector_count(disk);
    ramdisk_t* r = ram(disk);
    return r ? r->sectors : 0;
}

bool disk_present(u8 disk) { return disk_sectors(disk) != 0; }

static u32 hash(u8 disk, u64 lba) {
    return (u32)((lba / CACHE_SECTORS * 31 + disk) % bucket_count);
}

static void touch(cache_block_t* b) {
    if (b == newest) return;
    if (b->older) b->older->newer = b->newer;
    else oldest = b->newer;
    if (b->newer) b->newer->older = b->older;
    b->older = newest;
    b->newer = NULL;
    if (newest) newest->newer = b;
    else oldest = b;
    newest = b;
}

static bool writeback(cache_block_t* b) {
    if (!b->dirty) return true;
    if (!ata_write_multi(b->disk, (u32)b->lba, b->sectors, b->data)) {
        cache_stats.errors++;
        return false;
    }
    cache_stats.writebacks++;
    return true;
}

// Failed writeback/FLUSH keeps the block dirty and in the cache, so both
// reads and a later sync see the same data. Only a durable victim is reused.
static cache_block_t* get_block(u8 disk, u64 lba, u64 capacity, bool overwrite) {
    u32 h = hash(disk, lba);
    for (cache_block_t* b = buckets[h]; b; b = b->hash_next) {
        if (b->disk == disk && b->lba == lba) {
            cache_stats.hits++;
            touch(b);
            return b;
        }
    }
    cache_stats.misses++;
    cache_block_t* b;
    if (used_blocks < block_count) {
        b = &blocks[used_blocks++];
        b->older = newest;
        b->newer = NULL;
        if (newest) newest->newer = b;
        else oldest = b;
        newest = b;
    } else {
        b = oldest;
        if (b->dirty) {
            if (!writeback(b)) return NULL;
            if (!ata_flush(b->disk)) { cache_stats.errors++; return NULL; }
            b->dirty = false;
        }
        if (b->valid) {
            cache_block_t** link = &buckets[hash(b->disk, b->lba)];
            while (*link != b) link = &(*link)->hash_next;
            *link = b->hash_next;
        }
        touch(b);
    }
    b->valid = false;
    b->dirty = false;
    b->disk = disk;
    b->lba = lba;
    b->sectors = capacity - lba < CACHE_SECTORS ? capacity - lba : CACHE_SECTORS;
    if (!overwrite && !ata_read_multi(disk, (u32)lba, b->sectors, b->data)) {
        cache_stats.errors++;
        return NULL;
    }
    b->valid = true;
    b->hash_next = buckets[h];
    buckets[h] = b;
    return b;
}

static bool transfer(u8 disk, u64 lba, u32 count, void* buf, bool write) {
    u64 capacity = disk_sectors(disk);
    if (!capacity || lba > capacity || count > capacity - lba) return false;
    if (!count) return true;
    if (!buf) return false;
    ramdisk_t* r = ram(disk);
    if (r) {
        if (write) memcpy(r->base + lba * 512, buf, (u64)count * 512);
        else memcpy(buf, r->base + lba * 512, (u64)count * 512);
        return true;
    }
    if (!block_count) {
        return write ? ata_write_multi(disk, (u32)lba, count, buf)
                     : ata_read_multi(disk, (u32)lba, count, buf);
    }
    u8* p = buf;
    while (count) {
        u64 base = lba & ~7ULL;
        u32 offset = (u32)(lba - base), n = CACHE_SECTORS - offset;
        if (n > count) n = count;
        u32 sectors = capacity - base < CACHE_SECTORS ? capacity - base : CACHE_SECTORS;
        cache_block_t* b = get_block(disk, base, capacity, write && !offset && n == sectors);
        if (!b) return false;
        if (write) {
            memcpy(b->data + offset * 512, p, n * 512);
            b->dirty = true;
        } else memcpy(p, b->data + offset * 512, n * 512);
        lba += n;
        count -= n;
        p += n * 512;
    }
    return true;
}

// The kernel is single-core. Preserve IRQ state around the shared cache and
// polled ATA channel so preemption cannot interleave commands or LRU updates.
bool disk_read(u8 disk, u64 lba, u32 count, void* buf) {
    u64 flags = irq_save();
    bool ok = transfer(disk, lba, count, buf, false);
    irq_restore(flags);
    return ok;
}

bool disk_write(u8 disk, u64 lba, u32 count, const void* buf) {
    u64 flags = irq_save();
    bool ok = transfer(disk, lba, count, (void*)buf, true);
    irq_restore(flags);
    return ok;
}

bool disk_flush(u8 disk) {
    u64 flags = irq_save();
    bool ok = disk_present(disk);
    if (ok && disk < DISK_RAM0) {
        for (u32 i = 0; i < used_blocks; i++) {
            cache_block_t* b = &blocks[i];
            if (b->valid && b->disk == disk) ok = writeback(b) && ok;
        }
        // Even after a write error, flush the writes that did reach hardware.
        if (!ata_flush(disk)) { cache_stats.errors++; ok = false; }
        if (ok) for (u32 i = 0; i < used_blocks; i++)
            if (blocks[i].disk == disk) blocks[i].dirty = false;
    }
    irq_restore(flags);
    return ok;
}

void disk_cache_poll(void) {
    u64 flags = irq_save();
    u64 now = time_boot_ns();
    if (!block_count || now < next_flush) { irq_restore(flags); return; }
    // Bound each deferred batch to 128 KiB. Keep dirty data on every error;
    // the next five-second pass retries it without a tight failing-I/O loop.
    bool pending = false;
    u32 written = 0;
    for (u8 disk = 0; disk < DISK_RAM0; disk++) {
        cache_block_t* batch[CACHE_POLL_BLOCKS];
        u32 n = 0;
        bool ok = true;
        for (cache_block_t* b = oldest; b; b = b->newer) {
            if (!b->valid || !b->dirty || b->disk != disk) continue;
            if (written == CACHE_POLL_BLOCKS) { pending = true; break; }
            batch[n++] = b;
            written++;
            ok = writeback(b) && ok;
        }
        if (n) {
            if (!ata_flush(disk)) { cache_stats.errors++; ok = false; }
            if (ok) for (u32 i = 0; i < n; i++) batch[i]->dirty = false;
            else pending = false;
        }
    }
    next_flush = now + (pending ? 1000000ULL : CACHE_FLUSH_NS);
    irq_restore(flags);
}

void disk_cache_stats(disk_cache_stats_t* out) {
    if (!out) return;
    u64 flags = irq_save();
    *out = cache_stats;
    out->blocks = block_count;
    out->dirty = 0;
    for (u32 i = 0; i < used_blocks; i++) if (blocks[i].dirty) out->dirty++;
    irq_restore(flags);
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
