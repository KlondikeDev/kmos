// This file has been generated in whole or in part by a Large Language Model

#include <pagecache.h>
#include <memory.h>
#include <memcpy.h>
#include <memset.h>
#include <errno.h>

#define PAGE 4096ULL
typedef struct {
    const mount_t* mount;
    u64 key, off, frame, used;
} cached_page_t;
static cached_page_t* pages;
static u32 capacity;
static u64 clock;

static u64 key(const vnode_t* node) { return node->aux ? node->aux : node->id; }

static void drop(cached_page_t* p) {
    if (p->frame) pmm_free(p->frame);
    p->frame = 0;
}

void pagecache_forget(const vnode_t* node) {
    if (node->type != VN_REG) return;
    for (u32 i = 0; i < capacity; i++) {
        if (pages[i].frame && pages[i].mount == node->mnt && pages[i].key == key(node)) drop(&pages[i]);
    }
}

void pagecache_reclaim(void) {
    for (u32 i = 0; i < capacity; i++) {
        if (pages[i].frame && pmm_refs(pages[i].frame) == 1) drop(&pages[i]);
    }
}

u64 pagecache_pages(void) {
    u64 n = 0;
    for (u32 i = 0; i < capacity; i++) if (pages[i].frame) n++;
    return n;
}

i64 pagecache_get(vnode_t* node, u64 off) {
    vfs_getattr(node);
    if ((off & (PAGE - 1)) || off >= node->size) return -ENXIO;
    if (!pages) {
        u64 n = pmm_count_free() / 64;       // at most 16 MiB on larger machines
        if (n > 4096) n = 4096;
        if (n) {
            pages = kmalloc(n * sizeof(*pages));
            if (pages) { capacity = (u32)n; memset(pages, 0, capacity * sizeof(*pages)); }
        }
    }
    // Keep faults bounded to four probes. Scanning the entire cache on
    // every executable-page fault adds work proportional to reserved RAM.
    u64 hash = ((u64)node->mnt >> 4) ^ key(node) ^ (off / PAGE);
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    u32 first = capacity ? (hash % ((capacity + 3) / 4)) * 4 : 0;
    u32 end = first + 4 < capacity ? first + 4 : capacity;
    cached_page_t* victim = NULL;
    for (u32 i = first; i < end; i++) {
        cached_page_t* p = &pages[i];
        if (p->frame && p->mount == node->mnt && p->key == key(node) && p->off == off) {
            p->used = ++clock;
            pmm_ref(p->frame);
            return (i64)p->frame;
        }
        if (!victim || !p->frame || (victim->frame && p->used < victim->used)) victim = p;
    }
    if (victim) drop(victim);
    void* physical = pmm_alloc();
    if (!physical) { pagecache_reclaim(); physical = pmm_alloc(); }
    if (!physical) return -ENOMEM;
    void* data = (void*)((u64)physical + vmm_hhdm());
    memset(data, 0, PAGE);
    u64 want = node->size - off;
    if (want > PAGE) want = PAGE;
    if (vfs_read(node, off, data, want) != (i64)want) {
        pmm_free((u64)physical);
        return -EIO;
    }
    if (victim) {
        *victim = (cached_page_t){node->mnt, key(node), off, (u64)physical, ++clock};
        pmm_ref((u64)physical);             // caller and cache each own one
    }
    return (i64)(u64)physical;
}

i64 pagecache_read(vnode_t* node, u64 off, void* dst, u64 len) {
    u64 done = 0;
    while (done < len) {
        u64 offset = off & (PAGE - 1), n = PAGE - offset;
        if (n > len - done) n = len - done;
        i64 frame = pagecache_get(node, off - offset);
        if (frame < 0) return frame;
        memcpy((u8*)dst + done, (const void*)((u64)frame + vmm_hhdm() + offset), n);
        pmm_free((u64)frame);
        off += n;
        done += n;
    }
    return (i64)done;
}
