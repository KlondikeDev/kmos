// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// Disks by number (include/disk.h): ATA, NVMe, GPT slices and boot-module
// ramdisks. Slices share their physical parent's cache. A ramdisk keeps
// private writes only until shutdown, preserving the original install source.

#include <disk.h>
#include <ata.h>
#include <nvme.h>
#include <gpt.h>
#include <errno.h>
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
    u8** overlay;                  // pristine boot module plus private 4KiB writes
} ramdisk_t;

static ramdisk_t ramdisks[DISK_RAM_COUNT];
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

static struct { bool valid, mounted; u32 generation; gpt_partition_t p; } partitions[DISK_MAX-DISK_PART0];
static bool mounted_disks[DISK_WHOLE_MAX];
static u32 generation = 1;
static i32 install_source = -1;
void disk_set_install_source(i32 disk) { install_source = disk; }
i32 disk_install_source(void) { return install_source; }
u8 disk_parent(u8 disk) {
    return disk >= DISK_PART0 && disk < DISK_MAX && partitions[disk-DISK_PART0].valid ? partitions[disk-DISK_PART0].p.parent : disk;
}
bool disk_overlaps(u8 a,u8 b) {
    if(disk_parent(a)!=disk_parent(b))return false;
    u64 sa=a>=DISK_PART0 && a<DISK_MAX && partitions[a-DISK_PART0].valid ? partitions[a-DISK_PART0].p.start:0;
    u64 sb=b>=DISK_PART0 && b<DISK_MAX && partitions[b-DISK_PART0].valid ? partitions[b-DISK_PART0].p.start:0;
    u64 na=disk_sectors(a),nb=disk_sectors(b);
    return na && nb && (sa<=sb ? sb-sa<na : sa-sb<nb);
}
u32 disk_generation(u8 disk) {
    return disk >= DISK_PART0 && disk < DISK_MAX ? partitions[disk-DISK_PART0].generation : 1;
}
u32 disk_partition_slot(u8 disk) {
    return disk >= DISK_PART0 && disk < DISK_MAX && partitions[disk-DISK_PART0].valid ? partitions[disk-DISK_PART0].p.slot : 0;
}
bool disk_partition_guid(u8 disk,u8 out[16]) {
    if (!disk_partition_slot(disk)) return false;
    memcpy(out,partitions[disk-DISK_PART0].p.guid,16); return true;
}
void disk_set_mounted(u8 disk,bool mounted) {
    if (disk >= DISK_PART0 && disk < DISK_MAX) partitions[disk-DISK_PART0].mounted=mounted;
    else if (disk < DISK_WHOLE_MAX) mounted_disks[disk]=mounted;
}
bool disk_in_use(u8 disk) {
    u8 parent=disk_parent(disk);
    if (parent < DISK_WHOLE_MAX && mounted_disks[parent]) return true;
    for(u32 i=0;i<DISK_MAX-DISK_PART0;i++)
        if(partitions[i].valid && partitions[i].p.parent==parent && partitions[i].mounted) return true;
    return false;
}
u32 disk_block_size(u8 disk) {
    disk=disk_parent(disk);
    return disk>=DISK_NVME0 && disk<DISK_PART0 ? nvme_block_size(disk-DISK_NVME0) : 512;
}
static bool backend_read(u8 d,u64 l,u32 n,void* p) {
    return d < DISK_RAM0 ? ata_read_multi(d,(u32)l,n,p) : nvme_read(d-DISK_NVME0,l,n,p);
}
static bool backend_write(u8 d,u64 l,u32 n,const void* p) {
    return d < DISK_RAM0 ? ata_write_multi(d,(u32)l,n,p) : nvme_write(d-DISK_NVME0,l,n,p);
}
static bool backend_flush(u8 d) { return d < DISK_RAM0 ? ata_flush(d) : nvme_flush(d-DISK_NVME0); }
u64 disk_sectors(u8 disk) {
    if (disk >= DISK_PART0) return disk<DISK_MAX && partitions[disk-DISK_PART0].valid ? partitions[disk-DISK_PART0].p.count : 0;
    if (disk >= DISK_NVME0) {
        u64 sectors=nvme_sectors(disk-DISK_NVME0);
        return sectors<=0x7fffffffffffffffULL/512 ? sectors : 0;
    }
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
    if (!backend_write(b->disk, b->lba, b->sectors, b->data)) {
        cache_stats.errors++;
        return false;
    }
    cache_stats.writebacks++;
    return true;
}

// Amortize a durable eviction over nearby dirty LRU entries. Keep every
// owner until all writes and the single device FLUSH succeed, so failed
// write-back can always be retried without losing the only resident copy.
static bool flush_victims(cache_block_t* victim) {
    cache_block_t* batch[CACHE_POLL_BLOCKS];u32 n=0;
    for(cache_block_t* b=victim;b && n<CACHE_POLL_BLOCKS;b=b->newer)
        if(b->valid && b->dirty && b->disk==victim->disk) batch[n++]=b;
    for(u32 i=0;i<n;i++) if(!writeback(batch[i]))return false;
    if(!backend_flush(victim->disk)) { cache_stats.errors++;return false; }
    for(u32 i=0;i<n;i++)batch[i]->dirty=false;
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
        if (b->dirty && !flush_victims(b)) return NULL;
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
    if (!overwrite && !backend_read(disk,lba,b->sectors,b->data)) {
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
    // Translate before cache lookup, so parent and partition accesses are
    // coherent even when a partition begins inside a cached 4KiB block.
    if (disk >= DISK_PART0) {
        lba += partitions[disk-DISK_PART0].p.start;
        disk = partitions[disk-DISK_PART0].p.parent;
        capacity = disk_sectors(disk);
    }
    ramdisk_t* r = ram(disk);
    if (r) {
        u8* p=buf;
        while(count) {
            u64 block=lba/8; u32 in=(u32)(lba%8), n=8-in;
            if(n>count) n=count;
            if(write && !r->overlay[block]) {
                u8* copy=kmalloc(4096); if(!copy) return false;
                u64 bytes=(r->sectors-block*8)*512; if(bytes>4096) bytes=4096;
                memcpy(copy,r->base+block*4096,bytes); r->overlay[block]=copy;
            }
            u8* data=r->overlay[block] ? r->overlay[block] : r->base+block*4096;
            if(write) memcpy(data+in*512,p,n*512); else memcpy(p,data+in*512,n*512);
            lba+=n;count-=n;p+=n*512;
        }
        return true;
    }
    if (!block_count) {
        return write ? backend_write(disk,lba,count,buf)
                     : backend_read(disk,lba,count,buf);
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
    disk=disk_parent(disk);
    if (ok && !ram(disk)) {
        for (u32 i = 0; i < used_blocks; i++) {
            cache_block_t* b = &blocks[i];
            if (b->valid && b->disk == disk) ok = writeback(b) && ok;
        }
        // Even after a write error, flush the writes that did reach hardware.
        if (!backend_flush(disk)) { cache_stats.errors++; ok = false; }
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
    for (u8 disk = 0; disk < DISK_WHOLE_MAX; disk++) {
        if (disk>=DISK_RAM0 && disk<DISK_NVME0) continue;
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
            if (!backend_flush(disk)) { cache_stats.errors++; ok = false; }
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
    if (!resp || ramdisk_count == DISK_RAM_COUNT) return -1;
    for (u64 i = 0; i < resp->module_count; i++) {
        struct limine_file* f = resp->modules[i];
        if (!f->path || !ends_with(f->path, name)) continue;
        ramdisk_t* r = &ramdisks[ramdisk_count];
        r->base = (u8*)f->address;
        r->sectors = f->size / 512;
        if (!r->sectors) return -1;
        u64 blocks=(r->sectors+7)/8;
        r->overlay=kmalloc(blocks*sizeof(u8*));
        if(!r->overlay) return -1;
        for(u64 b=0;b<blocks;b++) r->overlay[b]=NULL;
        klog(LOG_OKAY, "disk: %s in memory as disk %u (%u MiB)\n", f->path,
             (u32)(DISK_RAM0 + ramdisk_count), (u32)(f->size >> 20));
        return (i32)(DISK_RAM0 + ramdisk_count++);
    }
    return -1;
}

bool ramdisk_original_read(u8 disk,u64 lba,u32 count,void* out) {
    ramdisk_t* r=ram(disk);
    if(!r || lba>r->sectors || count>r->sectors-lba || (count && !out)) return false;
    if(count) memcpy(out,r->base+lba*512,(u64)count*512);
    return true;
}
i32 disk_rescan(u8 disk) {
    if(disk>=DISK_WHOLE_MAX || ram(disk) || !disk_present(disk)) return -EINVAL;
    if(disk_in_use(disk)) return -EBUSY;
    if(!disk_flush(disk)) return -EIO;
    for(u32 i=0;i<DISK_MAX-DISK_PART0;i++) if(partitions[i].valid && partitions[i].p.parent==disk) {
        partitions[i].valid=false;partitions[i].generation=++generation;
    }
    gpt_partition_t* found=kmalloc(GPT_MAX_PARTITIONS*sizeof(*found));
    if(!found) return -ENOMEM;
    i32 n=gpt_scan(disk,found,GPT_MAX_PARTITIONS);
    if(n<0) { kfree(found); return n; }
    u32 available=0;
    for(u32 i=0;i<DISK_MAX-DISK_PART0;i++) if(!partitions[i].valid || partitions[i].p.parent==disk) available++;
    if((u32)n>available) { kfree(found); return -ENOSPC; }
    for(u32 i=0;i<DISK_MAX-DISK_PART0;i++) if(partitions[i].valid && partitions[i].p.parent==disk) partitions[i].valid=false;
    u32 j=0;
    for(u32 i=0;j<(u32)n && i<DISK_MAX-DISK_PART0;i++) if(!partitions[i].valid) {
        partitions[i].p=found[j++]; partitions[i].valid=true; partitions[i].mounted=false;
        partitions[i].generation=++generation;
    }
    kfree(found); return n;
}
void disk_scan_partitions(void) {
    for(u8 d=0;d<DISK_WHOLE_MAX;d++) if(!ram(d) && disk_present(d)) disk_rescan(d);
}
static bool same_guid(const u8* a,const u8* b) {
    for(u32 i=0;i<16;i++) if(a[i]!=b[i]) return false;
    return true;
}
i32 disk_find_uuid(const u8 guid[16]) {
    i32 found=-1;
    for(u32 i=0;i<DISK_MAX-DISK_PART0;i++) if(partitions[i].valid && same_guid(partitions[i].p.guid,guid)) {
        if(found>=0) return -1; // duplicate UUIDs are ambiguous, never choose one
        found=(i32)(DISK_PART0+i);
    }
    return found;
}
