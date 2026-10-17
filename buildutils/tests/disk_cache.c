// This file has been generated in whole or in part by a Large Language Model
// The backend substitutes only hardware, allocation and the clock. All caching
// and sector splitting below run the production disk layer.
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <disk.h>
#include <gpt.h>
#include <errno.h>
#include <limine.h>
#include <klog.h>
static unsigned char media[2][35 * 512];
static unsigned reads, writes, flushes;
static int fail_read, fail_write, fail_flush;
static u64 now;
kernel_logs_t log_messages[4];
uint64_t pmm_count_free(void) { return 64; }
void* kmalloc(uint64_t n) { return calloc(1, n); }
void kfree(void* p) { free(p); }
u64 time_boot_ns(void) { return now; }
void klog(kernel_logs_t* level, const char* fmt, ...) { (void)level; (void)fmt; }
bool ata_present(u8 disk) { return disk < 2; }
u64 ata_sector_count(u8 disk) { return disk < 2 ? 35 : 0; }
bool ata_read_multi(u8 disk, u32 lba, u32 n, void* p) {
    assert(disk < 2 && lba <= 35 && n <= 35 - lba);
    reads++;
    if (fail_read) return false;
    memcpy(p, media[disk] + lba * 512, n * 512);
    return true;
}
bool ata_write_multi(u8 disk, u32 lba, u32 n, const void* p) {
    assert(disk < 2 && lba <= 35 && n <= 35 - lba);
    writes++;
    if (fail_write) return false;
    memcpy(media[disk] + lba * 512, p, n * 512);
    return true;
}
bool ata_flush(u8 disk) { assert(disk < 2); flushes++; return !fail_flush; }
void nvme_init(void) {}
u32 nvme_count(void) { return 0; }
u64 nvme_sectors(u8 d) { (void)d;return 0; }
u32 nvme_block_size(u8 d) { (void)d;return 512; }
bool nvme_read(u8 d,u64 l,u32 n,void* p) { (void)d;(void)l;(void)n;(void)p;return false; }
bool nvme_write(u8 d,u64 l,u32 n,const void* p) { (void)d;(void)l;(void)n;(void)p;return false; }
bool nvme_flush(u8 d) { (void)d;return false; }
i32 gpt_scan(u8 disk,gpt_partition_t* out,u32 cap) {
 assert(cap);*out=(gpt_partition_t){.parent=disk,.slot=1,.start=1,.count=20,.guid={1}};return 1;
}
#include "../../drivers/disk.c"
int main(void) {
    unsigned char buf[4096], patch[1024];
    memset(media[0], 0x31, sizeof(media[0]));
    memset(media[1], 0x62, sizeof(media[1]));
    disk_cache_init();
    if(getenv("KUNIX_HOST_BATCH")) {
        memset(buf,0x5b,sizeof(buf));
        assert(disk_write(0,0,8,buf) && disk_write(0,8,8,buf));
        assert(!writes && !flushes);
        assert(disk_write(0,16,8,buf) && disk_write(0,24,8,buf));
        assert(writes==2 && flushes==1);
        fail_flush=1;
        assert(!disk_write(0,0,8,buf));
        disk_cache_stats_t batch_stats;disk_cache_stats(&batch_stats);
        assert(batch_stats.dirty==2);
        fail_flush=0;
        assert(disk_write(0,0,8,buf));
        assert(disk_read(0,24,8,buf) && buf[0]==0x5b);
        puts("disk cache: bounded eviction batching passed");return 0;
    }
    /* Removing caching must fail the repeated read and untouched-media checks. */
    assert(disk_read(0, 0, 1, buf));
    assert(disk_read(0, 0, 1, buf));
    assert(reads == 1 && buf[0] == 0x31);
    assert(disk_read(1, 0, 1, buf) && buf[0] == 0x62);
    memset(patch, 0xa5, sizeof(patch));
    assert(disk_write(0, 1, 1, patch));
    assert(media[0][512] == 0x31);
    assert(disk_read(0, 0, 8, buf));
    assert(buf[0] == 0x31 && buf[512] == 0xa5 && buf[1024] == 0x31);
    /* Touch disk 1: disk 0 is the dirty LRU victim now. */
    assert(disk_read(1, 0, 1, buf));
    fail_write = 1;
    assert(!disk_read(0, 8, 1, buf));
    assert(disk_read(0, 1, 1, buf) && buf[0] == 0xa5);
    assert(disk_read(1, 0, 1, buf));
    fail_write = 0;
    fail_flush = 1;
    assert(!disk_read(0, 8, 1, buf));
    assert(disk_read(0, 1, 1, buf) && buf[0] == 0xa5);
    assert(disk_read(1, 0, 1, buf));
    fail_flush = 0;
    assert(disk_read(0, 8, 1, buf));
    assert(media[0][512] == 0xa5 && media[1][512] == 0x62);
    /* Two-sector write straddles blocks. Both neighbors must survive. */
    assert(disk_write(0, 7, 2, patch));
    assert(disk_read(0, 6, 4, buf));
    assert(buf[0] == 0x31 && buf[512] == 0xa5 && buf[1024] == 0xa5 && buf[1536] == 0x31);
    fail_flush = 1;
    assert(!disk_flush(0));
    disk_cache_stats_t stats;
    disk_cache_stats(&stats);
    assert(stats.dirty == 2);
    fail_flush = 0;
    assert(disk_flush(0));
    disk_cache_stats(&stats);
    assert(!stats.dirty && media[0][7*512] == 0xa5 && media[0][8*512] == 0xa5);
    /* The last cache block has only 3 sectors; read-ahead must stop there. */
    assert(disk_read(0, 34, 1, buf) && buf[0] == 0x31);
    assert(disk_write(0, 34, 1, patch));
    assert(disk_flush(0));
    assert(media[0][33*512] == 0x31 && media[0][34*512] == 0xa5);
    unsigned before = reads + writes;
    assert(!disk_read(0, 35, 1, buf));
    assert(!disk_write(0, ~0ULL, 2, patch));
    assert(!disk_read(2, 0, 1, buf));
    assert(!disk_read(8, 0, 1, buf));
    assert(disk_read(0, 35, 0, NULL));
    assert(!disk_read(0, 0, 1, NULL));
    assert(reads + writes == before);
    /* Failed fills must not leave a valid block full of stale bytes. */
    fail_read = 1;
    assert(!disk_read(0, 16, 1, buf));
    fail_read = 0;
    assert(disk_read(0, 16, 1, buf) && buf[0] == 0x31);
    /* Full-block writes avoid read-before-write. */
    memset(buf, 0x77, sizeof(buf));
    before = reads;
    assert(disk_write(0, 24, 8, buf));
    assert(reads == before);
    before = writes;
    disk_cache_poll();
    assert(writes == before);
    now = 5000000000ULL;
    fail_write = 1;
    disk_cache_poll();
    disk_cache_stats(&stats);
    assert(stats.dirty == 1);
    fail_write = 0;
    now += 5000000000ULL;
    disk_cache_poll();
    disk_cache_stats(&stats);
    assert(!stats.dirty && media[0][24*512] == 0x77);
    /* Ramdisks bypass caching, preserving their immediate memory semantics. */
    unsigned char image[1024] = {0};
    struct limine_file file = {.path = "/root.img", .address = image, .size = sizeof(image)};
    struct limine_file* files[] = {&file};
    struct limine_module_response modules = {.module_count = 1, .modules = files};
    module_request.response = &modules;
    assert(ramdisk_from_module("root.img") == 4);
    before = reads + writes + flushes;
    assert(disk_write(4, 1, 1, patch));
    assert(image[512] != 0xa5);
    assert(ramdisk_original_read(4,1,1,buf) && buf[0]==image[512]);
    assert(disk_read(4, 1, 1, buf) && buf[0] == 0xa5);
    assert(!disk_read(4, 2, 1, buf));
    assert(disk_flush(4));
    assert(reads + writes + flushes == before);
    assert(disk_rescan(0)==1 && disk_sectors(DISK_PART0)==20);
    assert(disk_overlaps(0,DISK_PART0) && disk_overlaps(DISK_PART0,DISK_PART0));
    assert(!disk_overlaps(1,DISK_PART0));
    u32 old_generation=disk_generation(DISK_PART0);
    memset(patch,0x29,sizeof(patch));
    assert(disk_write(DISK_PART0,0,1,patch));
    assert(disk_read(0,1,1,buf) && buf[0]==0x29);
    memset(patch,0x54,sizeof(patch));
    assert(disk_write(0,2,1,patch));
    assert(disk_read(DISK_PART0,1,1,buf) && buf[0]==0x54);
    assert(!disk_read(DISK_PART0,20,1,buf));
    disk_set_mounted(DISK_PART0,true);
    assert(disk_in_use(0) && disk_rescan(0)==-EBUSY);
    disk_set_mounted(DISK_PART0,false);
    assert(disk_rescan(0)==1 && disk_generation(DISK_PART0)!=old_generation);
    printf("disk cache: passed (LRU, dirty eviction, retry, sectors, bounds, disks, periodic flush, ramdisk)\n");
    return 0;
}
