// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// UFS2 driver, read-write. See include/ufs2.h for the on-disk vocabulary.
//
// Everything on the disk is treated as untrusted: block addresses, sizes and
// inode numbers are range-checked before use, so a corrupt image produces
// errors rather than wild reads or writes.
//
// What it maintains, so that FreeBSD's fsck_ffs finds nothing to fix:
//   - inodes, including di_blocks (sectors held, indirect blocks counted);
//   - FFS's fragment rule: a file's last block, if it is one of the 12
//     direct ones, holds only as many fragments as the size needs; growing
//     the file grows that tail in place or moves it;
//   - each cylinder group's inode and fragment bitmaps. After every change
//     to a group's fragment map its derived counts -- free blocks, free
//     fragments, runs of each length (cg_frsum), the cluster map and
//     cluster summary -- are recomputed from the map, exactly as fsck's
//     pass 5 computes them, rather than patched up incrementally;
//   - the per-group summary array (fs_csaddr) and the superblock totals;
//   - directories in 512-byte chunks, "." and ".." first, link counts.
//
// Writes go straight to the disk, but metadata (the cylinder group being
// worked on, the summary array, the superblock) is written once at the end
// of each VFS operation, followed by a single cache flush. There is no
// journal and no soft updates: a crash in the middle of an operation can
// leave work for fsck, and the filesystem is marked dirty (fs_clean = 0)
// from the first write on.

#include <types.h>
#include <ufs2.h>
#include <vfs.h>
#include <ata.h>
#include <klog.h>
#include <memory.h>
#include <memcpy.h>
#include <memset.h>
#include <errno.h>
#include <rtc.h>
#include <usermode.h>
#include <klibc/string.h>
#include <file.h>

typedef struct {
    u8 drive;
    const struct mount* self;    // where it is mounted (set at the first root())
    ufs2_fs_t fs;                // the superblock, in memory
    u8* sbbuf;                   // fs_sbsize bytes: what gets written back
    u64 sbloc;
    ufs2_csum_t* csum;           // fs_ncg entries, from fs_csaddr
    u32 csum_bytes;
    bool writable;

    // Geometry, from the superblock.
    u32 bsize, fsize, frag, frag_shift, bshift;
    u32 ncg, ipg, fpg, inopb, nindir;
    u64 nfrags;                  // fs_size

    // The cylinder group being worked on.
    u8* cgbuf;
    i64 cg_loaded;
    bool cg_dirty, cs_dirty, sb_dirty, io_dirty;

    // A one-sector read cache: inode reads and directory scans hit the same
    // sector over and over, and every miss is a polled PIO transfer.
    u8 sec[512];
    u64 sec_lba;
} ufs_t;

static ufs_t* U(const vnode_t* n) {
    return (ufs_t*)n->mnt->priv;
}

// ---------------------------------------------------------------------------
// The device
// ---------------------------------------------------------------------------

static bool sector_read(ufs_t* u, u64 lba) {
    if (lba == u->sec_lba) return true;
    if (lba > 0xFFFFFFFULL || !ata_read_multi(u->drive, (u32)lba, 1, u->sec)) {
        u->sec_lba = ~0ULL;
        return false;
    }
    u->sec_lba = lba;
    return true;
}

static bool dev_read(ufs_t* u, u64 off, void* dst, u64 len) {
    u8* out = (u8*)dst;
    while (len) {
        u64 lba = off / 512;
        u32 in = (u32)(off % 512);
        if (in == 0 && len >= 512 && lba != u->sec_lba) {
            // Whole sectors: one transfer for as many as there are.
            u64 n = len / 512;
            if (n > 256) n = 256;
            if (lba + n > 0xFFFFFFFULL || !ata_read_multi(u->drive, (u32)lba, (u32)n, out)) return false;
            out += n * 512;
            off += n * 512;
            len -= n * 512;
            continue;
        }
        if (!sector_read(u, lba)) return false;
        u32 n = 512 - in;
        if (n > len) n = (u32)len;
        memcpy(out, u->sec + in, n);
        out += n;
        off += n;
        len -= n;
    }
    return true;
}

static bool dev_write(ufs_t* u, u64 off, const void* src, u64 len) {
    const u8* in_buf = (const u8*)src;
    u->io_dirty = true;
    while (len) {
        u64 lba = off / 512;
        u32 in = (u32)(off % 512);
        if (in == 0 && len >= 512) {
            u64 n = len / 512;
            if (n > 256) n = 256;
            if (lba + n > 0xFFFFFFFULL || !ata_write_multi(u->drive, (u32)lba, (u32)n, in_buf)) return false;
            if (u->sec_lba >= lba && u->sec_lba < lba + n) u->sec_lba = ~0ULL;
            in_buf += n * 512;
            off += n * 512;
            len -= n * 512;
            continue;
        }
        // A partial sector: read, modify, write -- and keep the cache current.
        if (!sector_read(u, lba)) return false;
        u32 n = 512 - in;
        if (n > len) n = (u32)len;
        memcpy(u->sec + in, in_buf, n);
        if (!ata_write_multi(u->drive, (u32)lba, 1, u->sec)) {
            u->sec_lba = ~0ULL;
            return false;
        }
        in_buf += n;
        off += n;
        len -= n;
    }
    return true;
}

static u8 zeros[4096];

static bool dev_zero(ufs_t* u, u64 off, u64 len) {
    while (len) {
        u64 n = len < sizeof(zeros) ? len : sizeof(zeros);
        if (!dev_write(u, off, zeros, n)) return false;
        off += n;
        len -= n;
    }
    return true;
}

static u64 frag_byte(ufs_t* u, u64 frag) {
    return frag * u->fsize;
}

static bool frag_valid(ufs_t* u, u64 frag) {
    return frag != 0 && frag < u->nfrags;
}

// ---------------------------------------------------------------------------
// Cylinder groups and the summaries
// ---------------------------------------------------------------------------

static u64 cgtod(ufs_t* u, u32 c) {
    return (u64)c * u->fpg + (u32)u->fs.fs_cblkno;
}

static bool cg_flush(ufs_t* u) {
    if (!u->cg_dirty) return true;
    u->cg_dirty = false;
    return dev_write(u, frag_byte(u, cgtod(u, (u32)u->cg_loaded)), u->cgbuf, (u32)u->fs.fs_cgsize);
}

static ufs2_cg_t* cg_get(ufs_t* u, u32 c) {
    if (c >= u->ncg) return NULL;
    if (u->cg_loaded == c) return (ufs2_cg_t*)u->cgbuf;
    if (!cg_flush(u)) return NULL;
    u->cg_loaded = -1;
    if (!dev_read(u, frag_byte(u, cgtod(u, c)), u->cgbuf, (u32)u->fs.fs_cgsize)) return NULL;
    ufs2_cg_t* cg = (ufs2_cg_t*)u->cgbuf;
    u32 size = (u32)u->fs.fs_cgsize;
    // The maps must lie inside the group block, or nothing below is safe.
    if (cg->cg_magic != UFS2_CG_MAGIC || cg->cg_cgx != c || cg->cg_ndblk > u->fpg ||
        cg->cg_iusedoff + (u->ipg + 7) / 8 > size || cg->cg_freeoff + (u->fpg + 7) / 8 > size ||
        (u->fs.fs_contigsumsize > 0 &&
         (cg->cg_clustersumoff + 4 * ((u32)u->fs.fs_contigsumsize + 1) > size ||
          cg->cg_clusteroff + (cg->cg_nclusterblks + 7) / 8 > size))) {
        klog(LOG_ERROR, "ufs2: cylinder group %u is corrupt\n", c);
        return NULL;
    }
    u->cg_loaded = c;
    return cg;
}

static u8* cg_inosused(ufs2_cg_t* cg) { return (u8*)cg + cg->cg_iusedoff; }
static u8* cg_blksfree(ufs2_cg_t* cg) { return (u8*)cg + cg->cg_freeoff; }
static bool bit(const u8* map, u32 i) { return (map[i >> 3] >> (i & 7)) & 1; }
static void set_bit(u8* map, u32 i) { map[i >> 3] |= (u8)(1 << (i & 7)); }
static void clr_bit(u8* map, u32 i) { map[i >> 3] &= (u8)~(1 << (i & 7)); }

// Recompute everything that is derived from the fragment map, the way
// fsck's pass 5 does.
static void cg_recount(ufs_t* u, ufs2_cg_t* cg) {
    u8* free = cg_blksfree(cg);
    i32 contig = u->fs.fs_contigsumsize;
    u8* cmap = contig > 0 ? (u8*)cg + cg->cg_clusteroff : NULL;
    u32* csum = contig > 0 ? (u32*)((u8*)cg + cg->cg_clustersumoff) : NULL;
    if (cmap) {
        memset(cmap, 0, (cg->cg_nclusterblks + 7) / 8);
        // Counts start at runs of 1: slot 0 is never used, and FFS lays the
        // array out so that it overlaps the last word of the fragment map.
        // Leave it alone.
        for (i32 i = 1; i <= contig; i++) csum[i] = 0;
    }
    for (u32 i = 0; i < UFS2_MAXFRAG; i++) cg->cg_frsum[i] = 0;
    i32 nbfree = 0, nffree = 0;
    for (u32 b = 0; b + u->frag <= cg->cg_ndblk; b += u->frag) {
        u32 nfree = 0;
        for (u32 j = 0; j < u->frag; j++) nfree += bit(free, b + j);
        if (nfree == u->frag) {
            nbfree++;
            if (cmap) set_bit(cmap, b / u->frag);
        } else if (nfree) {
            nffree += (i32)nfree;
            // Each maximal run of free fragments counts once, by length.
            u32 run = 0;
            for (u32 j = 0; j <= u->frag; j++) {
                if (j < u->frag && bit(free, b + j)) { run++; continue; }
                if (run) cg->cg_frsum[run]++;
                run = 0;
            }
        }
    }
    if (cmap) {
        u32 run = 0;
        for (u32 i = 0; i < cg->cg_nclusterblks; i++) {
            if (bit(cmap, i)) { run++; continue; }
            if (run) csum[run > (u32)contig ? (u32)contig : run]++;
            run = 0;
        }
        if (run) csum[run > (u32)contig ? (u32)contig : run]++;
    }
    cg->cg_cs.cs_nbfree = nbfree;
    cg->cg_cs.cs_nffree = nffree;
}

// A group changed: refresh its counts, the summary array and the totals.
static void cg_changed(ufs_t* u, ufs2_cg_t* cg, bool blocks) {
    if (blocks) cg_recount(u, cg);
    cg->cg_time = (i64)rtc_now();
    u->cg_dirty = true;
    u->csum[cg->cg_cgx] = cg->cg_cs;
    u->cs_dirty = true;
    ufs2_csum_total_t* t = &u->fs.fs_cstotal;
    t->cs_ndir = t->cs_nbfree = t->cs_nifree = t->cs_nffree = 0;
    for (u32 c = 0; c < u->ncg; c++) {
        t->cs_ndir += u->csum[c].cs_ndir;
        t->cs_nbfree += u->csum[c].cs_nbfree;
        t->cs_nifree += u->csum[c].cs_nifree;
        t->cs_nffree += u->csum[c].cs_nffree;
    }
    u->sb_dirty = true;
}

// Start a modifying operation. The first one marks the filesystem dirty.
static i32 begin_write(ufs_t* u) {
    if (!u->writable) return -EROFS;
    if (u->fs.fs_clean) {
        u->fs.fs_clean = 0;
        u->sb_dirty = true;
    }
    return 0;
}

// End of an operation: write what is pending, then one flush.
static i32 commit(ufs_t* u) {
    bool ok = cg_flush(u);
    if (u->cs_dirty) {
        u->cs_dirty = false;
        ok = dev_write(u, frag_byte(u, (u64)u->fs.fs_csaddr), u->csum, u->csum_bytes) && ok;
    }
    if (u->sb_dirty) {
        u->sb_dirty = false;
        u->fs.fs_time = (i64)rtc_now();
        memcpy(u->sbbuf, &u->fs, sizeof(u->fs));
        ok = dev_write(u, u->sbloc, u->sbbuf, (u32)u->fs.fs_sbsize) && ok;
    }
    if (u->io_dirty) {
        u->io_dirty = false;
        ok = ata_flush(u->drive) && ok;
    }
    return ok ? 0 : -EIO;
}

// ---------------------------------------------------------------------------
// Fragment allocation
// ---------------------------------------------------------------------------

// n fragments (1..frag) inside one block of group c; 0 if there is no room.
static u64 cg_alloc(ufs_t* u, u32 c, u32 n) {
    if (u->csum[c].cs_nbfree == 0 && (n == u->frag || (u32)u->csum[c].cs_nffree < n)) return 0;
    ufs2_cg_t* cg = cg_get(u, c);
    if (!cg) return 0;
    u8* free = cg_blksfree(cg);
    u32 nblk = cg->cg_ndblk / u->frag;
    u64 found = ~0ULL;

    // A partial request goes into a block already broken up, if one has a
    // big enough run, so whole blocks stay whole.
    if (n < u->frag) {
        for (u32 b = 0; b < nblk && found == ~0ULL; b++) {
            u32 base = b * u->frag, nfree = 0;
            for (u32 j = 0; j < u->frag; j++) nfree += bit(free, base + j);
            if (nfree == 0 || nfree == u->frag || nfree < n) continue;
            u32 run = 0;
            for (u32 j = 0; j < u->frag; j++) {
                run = bit(free, base + j) ? run + 1 : 0;
                if (run == n) { found = base + j + 1 - n; break; }
            }
        }
    }
    if (found == ~0ULL) {
        // A free block, looking onwards from where the last one was found.
        u32 start = cg->cg_rotor / u->frag;
        for (u32 k = 0; k < nblk; k++) {
            u32 b = (start + k) % nblk, base = b * u->frag, nfree = 0;
            for (u32 j = 0; j < u->frag; j++) nfree += bit(free, base + j);
            if (nfree == u->frag) {
                found = base;
                cg->cg_rotor = base;
                break;
            }
        }
    }
    if (found == ~0ULL) return 0;
    for (u32 j = 0; j < n; j++) clr_bit(free, (u32)found + j);
    cg_changed(u, cg, true);
    return (u64)c * u->fpg + found;
}

// n fragments, preferably in group pref. 0 means the disk is full.
static u64 frag_alloc(ufs_t* u, u32 pref, u32 n) {
    if (pref >= u->ncg) pref = 0;
    for (u32 k = 0; k < u->ncg; k++) {
        u64 f = cg_alloc(u, (pref + k) % u->ncg, n);
        if (f) return f;
    }
    return 0;
}

static void frag_free(ufs_t* u, u64 addr, u32 n) {
    u32 c = (u32)(addr / u->fpg), pos = (u32)(addr % u->fpg);
    ufs2_cg_t* cg = cg_get(u, c);
    if (!cg || pos + n > cg->cg_ndblk || (pos % u->frag) + n > u->frag) {
        klog(LOG_ERROR, "ufs2: refusing to free fragments %lu+%u: out of range\n", addr, n);
        return;
    }
    u8* free = cg_blksfree(cg);
    for (u32 j = 0; j < n; j++) {
        if (bit(free, pos + j)) klog(LOG_WARN, "ufs2: fragment %lu was already free\n", addr + j);
        set_bit(free, pos + j);
    }
    cg_changed(u, cg, true);
}

// ---------------------------------------------------------------------------
// Inodes
// ---------------------------------------------------------------------------

static bool ino_valid(ufs_t* u, u32 ino) {
    return ino >= UFS2_ROOT_INO && ino < u->ncg * u->ipg;
}

static u64 ino_byte(ufs_t* u, u32 ino) {
    u32 c = ino / u->ipg, idx = ino % u->ipg;
    // The group's inode table starts iblkno fragments into the group, and
    // packs inopb inodes into each block.
    u64 frag = (u64)c * u->fpg + (u32)u->fs.fs_iblkno + ((u64)(idx / u->inopb) << u->frag_shift);
    return frag_byte(u, frag) + (u64)(idx % u->inopb) * sizeof(ufs2_dinode_t);
}

static bool read_inode(ufs_t* u, u32 ino, ufs2_dinode_t* out) {
    if (!ino_valid(u, ino)) return false;
    return dev_read(u, ino_byte(u, ino), out, sizeof(*out));
}

static bool write_inode(ufs_t* u, u32 ino, const ufs2_dinode_t* ip) {
    if (!ino_valid(u, ino)) return false;
    return dev_write(u, ino_byte(u, ino), ip, sizeof(*ip));
}

static u32 cg_of_ino(ufs_t* u, u32 ino) {
    return ino / u->ipg;
}

// A free inode, near group pref (directories: in the group with the most
// free inodes, to spread them out). 0 if none.
static u32 inode_alloc(ufs_t* u, u32 pref, bool dir) {
    if (dir) {
        i32 best = -1;
        for (u32 c = 0; c < u->ncg; c++) {
            if (best < 0 || u->csum[c].cs_nifree > u->csum[best].cs_nifree) best = (i32)c;
        }
        pref = (u32)best;
    }
    for (u32 k = 0; k < u->ncg; k++) {
        u32 c = (pref + k) % u->ncg;
        if (u->csum[c].cs_nifree <= 0) continue;
        ufs2_cg_t* cg = cg_get(u, c);
        if (!cg) continue;
        u8* used = cg_inosused(cg);
        for (u32 i = 0; i < u->ipg; i++) {
            if (bit(used, i) || c * u->ipg + i < UFS2_ROOT_INO + 1) continue;
            // UFS2 initialises inode blocks lazily: zero the next block of
            // them before handing one out past cg_initediblk.
            while (i >= cg->cg_initediblk && cg->cg_initediblk < u->ipg) {
                u32 first = c * u->ipg + cg->cg_initediblk;
                if (!dev_zero(u, ino_byte(u, first), (u64)u->inopb * sizeof(ufs2_dinode_t))) return 0;
                cg->cg_initediblk += u->inopb;
            }
            set_bit(used, i);
            cg->cg_cs.cs_nifree--;
            if (dir) cg->cg_cs.cs_ndir++;
            cg->cg_irotor = i;
            cg_changed(u, cg, false);
            return c * u->ipg + i;
        }
    }
    return 0;
}

static void inode_free(ufs_t* u, u32 ino, bool dir) {
    ufs2_cg_t* cg = cg_get(u, cg_of_ino(u, ino));
    if (!cg) return;
    u32 i = ino % u->ipg;
    if (!bit(cg_inosused(cg), i)) {
        klog(LOG_WARN, "ufs2: inode %u was already free\n", ino);
        return;
    }
    clr_bit(cg_inosused(cg), i);
    cg->cg_cs.cs_nifree++;
    if (dir) cg->cg_cs.cs_ndir--;
    cg_changed(u, cg, false);
}

// A short symlink keeps its target in the block pointers themselves
// (di_db/di_ib) and holds no blocks: those pointers are text, not addresses.
static bool fast_link(ufs_t* u, const ufs2_dinode_t* ip) {
    return (ip->di_mode & UFS2_IFMT) == UFS2_IFLNK && ip->di_blocks == 0 &&
           ip->di_size < (u64)u->fs.fs_maxsymlinklen;
}

static void touch(ufs2_dinode_t* ip, bool modified) {
    i64 now = (i64)rtc_now();
    ip->di_ctime = now;
    ip->di_ctimensec = 0;
    if (modified) {
        ip->di_mtime = now;
        ip->di_mtimensec = 0;
    }
}

// ---------------------------------------------------------------------------
// Block mapping
// ---------------------------------------------------------------------------

// Fragments block lbn of a file of this size holds: all of them, except
// for the last block if it is a direct one -- that has just enough.
static u32 alloc_size(ufs_t* u, u64 size, u64 lbn) {
    if (size == 0) return 0;
    u64 last = (size - 1) >> u->bshift;
    if (lbn > last) return 0;
    if (lbn < last || lbn >= UFS2_NDADDR) return u->frag;
    u64 rem = size - (lbn << u->bshift);
    return (u32)((rem + u->fsize - 1) / u->fsize);
}

static void add_blocks(ufs_t* u, ufs2_dinode_t* ip, i64 frags) {
    ip->di_blocks += (u64)(frags * (i64)(u->fsize / 512));
}

// Where an indirect-mapped lbn sits: its level (0 single, 1 double, 2
// triple) and its offset within that level's range.
static bool indirect_path(ufs_t* u, u64 lbn, u32* level, u64* rel) {
    lbn -= UFS2_NDADDR;
    u64 span = u->nindir;
    for (u32 l = 0; l < UFS2_NIADDR; l++) {
        if (lbn < span) { *level = l; *rel = lbn; return true; }
        lbn -= span;
        span *= u->nindir;
    }
    return false;
}

// The fragment address of block lbn (>= NDADDR), or 0 for a hole. With
// alloc, missing indirect blocks and the data block itself are created
// (indirect blocks zeroed; *fresh says the data block is new and holds
// garbage). Returns 0, -EIO, -ENOSPC or -EFBIG.
static i32 bmap_indirect(ufs_t* u, ufs2_dinode_t* ip, u32 pref, u64 lbn, bool alloc,
                         u64* out, bool* fresh) {
    u32 level;
    u64 rel;
    if (fresh) *fresh = false;
    if (!indirect_path(u, lbn, &level, &rel)) return -EFBIG;
    u64 span = 1;
    for (u32 l = 0; l < level; l++) span *= u->nindir;

    u64 blk = (u64)ip->di_ib[level];
    u64 parent = 0;                                  // 0: the pointer is in the inode
    u64 slot = 0;
    for (i32 depth = (i32)level; ; depth--) {
        if (blk == 0) {
            if (!alloc) { *out = 0; return 0; }
            blk = frag_alloc(u, pref, u->frag);
            if (!blk) return -ENOSPC;
            add_blocks(u, ip, u->frag);
            bool leaf = depth < 0;
            if (!leaf && !dev_zero(u, frag_byte(u, blk), u->bsize)) return -EIO;
            if (leaf && fresh) *fresh = true;
            if (parent == 0) ip->di_ib[level] = (i64)blk;
            else if (!dev_write(u, frag_byte(u, parent) + slot * 8, &blk, 8)) return -EIO;
        } else if (!frag_valid(u, blk)) {
            return -EIO;
        }
        if (depth < 0) break;                        // blk is the data block
        slot = rel / span;
        rel %= span;
        span = depth ? span / u->nindir : 1;
        parent = blk;
        if (!dev_read(u, frag_byte(u, blk) + slot * 8, &blk, 8)) return -EIO;
    }
    *out = blk;
    return 0;
}

// Fragment address of logical block lbn, 0 for a hole, ~0 on error.
static u64 block_map(ufs_t* u, ufs2_dinode_t* ip, u64 lbn) {
    if (lbn < UFS2_NDADDR) return (u64)ip->di_db[lbn];
    u64 addr;
    if (bmap_indirect(u, ip, 0, lbn, false, &addr, NULL) < 0) return ~0ULL;
    return addr;
}

// Read len bytes of a file's data starting at off. Holes read as zeros.
static bool read_data(ufs_t* u, ufs2_dinode_t* ip, u64 off, void* dst, u64 len) {
    u8* out = (u8*)dst;
    while (len) {
        u64 lbn = off >> u->bshift;
        u32 boff = (u32)(off & (u->bsize - 1));
        u64 n = u->bsize - boff;
        if (n > len) n = len;
        u64 frag = block_map(u, ip, lbn);
        if (frag == ~0ULL) return false;
        if (frag == 0) {
            memset(out, 0, (u32)n);
        } else {
            if (!frag_valid(u, frag)) return false;
            if (!dev_read(u, frag_byte(u, frag) + boff, out, n)) return false;
        }
        out += n;
        off += n;
        len -= n;
    }
    return true;
}

// The direct block L holds `have` fragments and must hold `need`: extend it
// in place if the fragments after it are free, or move it.
static i32 grow_tail(ufs_t* u, ufs2_dinode_t* ip, u32 pref, u64 L, u32 have, u32 need) {
    u64 addr = (u64)ip->di_db[L];
    u32 c = (u32)(addr / u->fpg), pos = (u32)(addr % u->fpg);
    ufs2_cg_t* cg = cg_get(u, c);
    if (!cg) return -EIO;
    u8* free = cg_blksfree(cg);
    bool in_place = (pos % u->frag) + need <= u->frag;
    for (u32 j = have; in_place && j < need; j++) in_place = bit(free, pos + j);
    if (in_place) {
        for (u32 j = have; j < need; j++) clr_bit(free, pos + j);
        cg_changed(u, cg, true);
        add_blocks(u, ip, need - have);
        // Bytes past the old end of file must read back as zeros.
        return dev_zero(u, frag_byte(u, addr + have), (u64)(need - have) * u->fsize) ? 0 : -EIO;
    }
    u64 to = frag_alloc(u, pref, need);
    if (!to) return -ENOSPC;
    u8* buf = kmalloc(u->bsize);
    if (!buf) { frag_free(u, to, need); return -ENOMEM; }
    memset(buf, 0, u->bsize);
    bool ok = dev_read(u, frag_byte(u, addr), buf, (u64)have * u->fsize) &&
              dev_write(u, frag_byte(u, to), buf, (u64)need * u->fsize);
    kfree(buf);
    if (!ok) { frag_free(u, to, need); return -EIO; }
    frag_free(u, addr, have);
    ip->di_db[L] = (i64)to;
    add_blocks(u, ip, (i64)need - (i64)have);
    return 0;
}

// Write into a file's blocks, allocating as needed, and update its size.
// Returns bytes written or -errno (bytes are reported if any made it).
static i64 write_data(ufs_t* u, u32 ino, ufs2_dinode_t* ip, u64 off, const void* src, u64 len) {
    if (len == 0) return 0;
    if (off + len < off || off + len > u->fs.fs_maxfilesize) return -EFBIG;
    u32 pref = cg_of_ino(u, ino);
    u64 old = ip->di_size, want = off + len > old ? off + len : old;

    // The old last block may have been a short one; if the file now goes
    // past it, it must grow first.
    if (want > old && old > 0) {
        u64 L = (old - 1) >> u->bshift;
        if (L < UFS2_NDADDR && ip->di_db[L]) {
            u32 have = alloc_size(u, old, L), need = alloc_size(u, want, L);
            if (need > have) {
                i32 r = grow_tail(u, ip, pref, L, have, need);
                if (r < 0) return r;
            }
        }
    }

    const u8* in = (const u8*)src;
    u64 done = 0;
    i32 err = 0;
    u8* blockbuf = NULL;
    while (done < len) {
        u64 pos = off + done;
        u64 lbn = pos >> u->bshift;
        u32 boff = (u32)(pos & (u->bsize - 1));
        u64 n = u->bsize - boff;
        if (n > len - done) n = len - done;

        u64 addr;
        bool fresh = false;
        u32 extent = u->frag;                         // fragments the block holds
        if (lbn < UFS2_NDADDR) {
            extent = alloc_size(u, want, lbn);
            addr = (u64)ip->di_db[lbn];
            if (!addr) {
                addr = frag_alloc(u, pref, extent);
                if (!addr) { err = -ENOSPC; break; }
                ip->di_db[lbn] = (i64)addr;
                add_blocks(u, ip, extent);
                fresh = true;
            }
        } else {
            err = bmap_indirect(u, ip, pref, lbn, true, &addr, &fresh);
            if (err < 0) break;
        }
        if (!frag_valid(u, addr)) { err = -EIO; break; }

        bool ok;
        if (fresh && (boff != 0 || n != (u64)extent * u->fsize)) {
            // New space the write does not cover must read back as zeros.
            if (!blockbuf && !(blockbuf = kmalloc(u->bsize))) { err = -ENOMEM; break; }
            memset(blockbuf, 0, u->bsize);
            memcpy(blockbuf + boff, in + done, n);
            ok = dev_write(u, frag_byte(u, addr), blockbuf, (u64)extent * u->fsize);
        } else {
            ok = dev_write(u, frag_byte(u, addr) + boff, in + done, n);
        }
        if (!ok) { err = -EIO; break; }
        done += n;
    }
    if (blockbuf) kfree(blockbuf);
    if (off + done > ip->di_size) ip->di_size = off + done;
    touch(ip, true);
    if (!write_inode(u, ino, ip) && !err) err = -EIO;
    return done ? (i64)done : err;
}

// Free the parts of an indirect tree that map blocks past `last` (the last
// lbn to keep; -1 keeps none). ptr is the tree's root pointer; level 0 is a
// single indirect block. first is the first lbn the tree maps.
static i32 trim_indirect(ufs_t* u, ufs2_dinode_t* ip, i64* ptr, u32 level, i64 first, i64 last) {
    if (*ptr == 0) return 0;
    if (!frag_valid(u, (u64)*ptr)) return -EIO;
    u64 span = 1;
    for (u32 l = 0; l < level; l++) span *= u->nindir;
    i64* blk = kmalloc(u->bsize);
    if (!blk) return -ENOMEM;
    i32 err = 0;
    if (!dev_read(u, frag_byte(u, (u64)*ptr), blk, u->bsize)) { kfree(blk); return -EIO; }
    bool changed = false, empty = true;
    for (u32 i = 0; i < u->nindir; i++) {
        if (blk[i] == 0) continue;
        i64 efirst = first + (i64)(i * span);
        if (level == 0) {
            if (efirst > last) {
                if (frag_valid(u, (u64)blk[i])) {
                    frag_free(u, (u64)blk[i], u->frag);
                    add_blocks(u, ip, -(i64)u->frag);
                }
                blk[i] = 0;
                changed = true;
            }
        } else {
            i64 before = blk[i];
            err = trim_indirect(u, ip, &blk[i], level - 1, efirst, last);
            if (err < 0) break;
            if (blk[i] != before) changed = true;
        }
        if (blk[i]) empty = false;
    }
    if (!err) {
        if (empty) {
            frag_free(u, (u64)*ptr, u->frag);
            add_blocks(u, ip, -(i64)u->frag);
            *ptr = 0;
        } else if (changed && !dev_write(u, frag_byte(u, (u64)*ptr), blk, u->bsize)) {
            err = -EIO;
        }
    }
    kfree(blk);
    return err;
}

// Shrink (or grow) a file to `size`, giving back what it no longer needs.
static i32 truncate_inode(ufs_t* u, u32 ino, ufs2_dinode_t* ip, u64 size) {
    if (fast_link(u, ip)) {
        // Only ever truncated to nothing, as the link goes away.
        memset(ip->di_db, 0, sizeof(ip->di_db));
        memset(ip->di_ib, 0, sizeof(ip->di_ib));
        ip->di_size = 0;
        return write_inode(u, ino, ip) ? 0 : -EIO;
    }
    u64 old = ip->di_size;
    if (size > u->fs.fs_maxfilesize) return -EFBIG;
    if (size == old) {
        touch(ip, true);
        return write_inode(u, ino, ip) ? 0 : -EIO;
    }
    if (size > old) {
        // Growing: allocate the new last block (as FreeBSD does) by writing
        // its final byte; everything between reads as zeros.
        u8 z = 0;
        i64 r = write_data(u, ino, ip, size - 1, &z, 1);
        return r < 0 ? (i32)r : 0;
    }

    i64 last = size ? (i64)((size - 1) >> u->bshift) : -1;
    i64 first = UFS2_NDADDR;
    u64 span = u->nindir;
    for (u32 l = 0; l < UFS2_NIADDR; l++) {
        i64 root = ip->di_ib[l];                      // (di_ib is a packed field)
        i32 r = trim_indirect(u, ip, &root, l, first, last);
        ip->di_ib[l] = root;
        if (r < 0) return r;
        first += (i64)span;
        span *= u->nindir;
    }
    for (i64 lbn = UFS2_NDADDR - 1; lbn > last; lbn--) {
        if (!ip->di_db[lbn]) continue;
        u32 n = alloc_size(u, old, (u64)lbn);
        if (frag_valid(u, (u64)ip->di_db[lbn])) frag_free(u, (u64)ip->di_db[lbn], n);
        add_blocks(u, ip, -(i64)n);
        ip->di_db[lbn] = 0;
    }
    if (last >= 0) {
        u64 addr = block_map(u, ip, (u64)last);
        u32 keep = alloc_size(u, size, (u64)last);
        if (last < UFS2_NDADDR && addr) {
            u32 had = alloc_size(u, old, (u64)last);
            if (keep < had) {
                frag_free(u, addr + keep, had - keep);
                add_blocks(u, ip, -(i64)(had - keep));
            }
        }
        // What stays allocated past the new end must be zero, or growing
        // the file again would bring the old bytes back.
        u32 tail = (u32)(size & (u->bsize - 1));
        if (addr && addr != ~0ULL && tail) {
            u64 end = (u64)keep * u->fsize;
            if (!dev_zero(u, frag_byte(u, addr) + tail, end - tail)) return -EIO;
        }
    }
    ip->di_size = size;
    touch(ip, true);
    return write_inode(u, ino, ip) ? 0 : -EIO;
}

// ---------------------------------------------------------------------------
// Directories
// ---------------------------------------------------------------------------

// Call visit for every live entry; stops early when visit returns true.
// Returns false on a corrupt directory.
typedef bool (*dir_visit_t)(const char* name, u32 namlen, u32 ino, void* ctx);

static bool chunk_ok(const u8* chunk) {
    u32 pos = 0;
    while (pos < UFS2_DIRBLKSIZ) {
        const ufs2_direct_t* d = (const ufs2_direct_t*)(chunk + pos);
        if (pos + sizeof(ufs2_direct_t) > UFS2_DIRBLKSIZ || d->d_reclen < sizeof(ufs2_direct_t) ||
            (d->d_reclen & 3) || pos + d->d_reclen > UFS2_DIRBLKSIZ ||
            (d->d_ino && UFS2_DIRSIZ(d->d_namlen) > d->d_reclen)) {
            return false;
        }
        pos += d->d_reclen;
    }
    return true;
}

static bool dir_walk(ufs_t* u, ufs2_dinode_t* dir, dir_visit_t visit, void* ctx) {
    u8 chunk[UFS2_DIRBLKSIZ];
    for (u64 base = 0; base + UFS2_DIRBLKSIZ <= dir->di_size; base += UFS2_DIRBLKSIZ) {
        if (!read_data(u, dir, base, chunk, UFS2_DIRBLKSIZ) || !chunk_ok(chunk)) return false;
        for (u32 pos = 0; pos < UFS2_DIRBLKSIZ; ) {
            ufs2_direct_t* d = (ufs2_direct_t*)(chunk + pos);
            if (d->d_ino && visit((const char*)(d + 1), d->d_namlen, d->d_ino, ctx)) return true;
            pos += d->d_reclen;
        }
    }
    return true;
}

static bool name_is(const ufs2_direct_t* d, const char* name, u32 len) {
    if (!d->d_ino || d->d_namlen != len) return false;
    const char* n = (const char*)(d + 1);
    for (u32 i = 0; i < len; i++) if (n[i] != name[i]) return false;
    return true;
}

// Find an entry: its inode, and where it is (chunk offset, position, and
// the position of the entry before it in the chunk, or ~0 if it is first).
static i32 dir_find(ufs_t* u, ufs2_dinode_t* dir, const char* name, u32 len, u32* ino,
                    u64* chunk_off, u32* pos_out, u32* prev_out, u8* chunk) {
    for (u64 base = 0; base + UFS2_DIRBLKSIZ <= dir->di_size; base += UFS2_DIRBLKSIZ) {
        if (!read_data(u, dir, base, chunk, UFS2_DIRBLKSIZ) || !chunk_ok(chunk)) return -EIO;
        u32 prev = ~0u;
        for (u32 pos = 0; pos < UFS2_DIRBLKSIZ; ) {
            ufs2_direct_t* d = (ufs2_direct_t*)(chunk + pos);
            if (name_is(d, name, len)) {
                *ino = d->d_ino;
                if (chunk_off) *chunk_off = base;
                if (pos_out) *pos_out = pos;
                if (prev_out) *prev_out = prev;
                return 0;
            }
            prev = pos;
            pos += d->d_reclen;
        }
    }
    return -ENOENT;
}

static void put_entry(u8* at, u32 ino, u16 reclen, u8 type, const char* name, u32 len) {
    ufs2_direct_t* d = (ufs2_direct_t*)at;
    d->d_ino = ino;
    d->d_reclen = reclen;
    d->d_type = type;
    d->d_namlen = (u8)len;
    char* n = (char*)(d + 1);
    for (u32 i = 0; i < len; i++) n[i] = name[i];
    // The NUL and the padding up to the next 4-byte boundary are zero.
    for (u32 i = len; i < UFS2_DIRSIZ(len) - sizeof(ufs2_direct_t); i++) n[i] = 0;
}

// Add an entry to directory dino, which must not have one by that name.
static i32 dir_add(ufs_t* u, u32 dino, const char* name, u32 len, u32 ino, u8 type) {
    ufs2_dinode_t dir;
    if (!read_inode(u, dino, &dir)) return -EIO;
    u32 need = UFS2_DIRSIZ(len);
    u8 chunk[UFS2_DIRBLKSIZ];
    for (u64 base = 0; base + UFS2_DIRBLKSIZ <= dir.di_size; base += UFS2_DIRBLKSIZ) {
        if (!read_data(u, &dir, base, chunk, UFS2_DIRBLKSIZ) || !chunk_ok(chunk)) return -EIO;
        for (u32 pos = 0; pos < UFS2_DIRBLKSIZ; ) {
            ufs2_direct_t* d = (ufs2_direct_t*)(chunk + pos);
            u32 used = d->d_ino ? UFS2_DIRSIZ(d->d_namlen) : 0;
            if (d->d_reclen - used >= need) {
                if (used == 0) {
                    put_entry(chunk + pos, ino, d->d_reclen, type, name, len);
                } else {
                    u16 rest = (u16)(d->d_reclen - used);
                    d->d_reclen = (u16)used;
                    put_entry(chunk + pos + used, ino, rest, type, name, len);
                }
                i64 r = write_data(u, dino, &dir, base, chunk, UFS2_DIRBLKSIZ);
                return r < 0 ? (i32)r : 0;
            }
            pos += d->d_reclen;
        }
    }
    // No room: a new chunk on the end, the entry taking all of it.
    memset(chunk, 0, sizeof(chunk));
    put_entry(chunk, ino, UFS2_DIRBLKSIZ, type, name, len);
    i64 r = write_data(u, dino, &dir, dir.di_size, chunk, UFS2_DIRBLKSIZ);
    return r < 0 ? (i32)r : 0;
}

static i32 dir_remove(ufs_t* u, u32 dino, const char* name, u32 len) {
    ufs2_dinode_t dir;
    if (!read_inode(u, dino, &dir)) return -EIO;
    u8 chunk[UFS2_DIRBLKSIZ];
    u32 ino, pos, prev;
    u64 base;
    i32 r = dir_find(u, &dir, name, len, &ino, &base, &pos, &prev, chunk);
    if (r < 0) return r;
    ufs2_direct_t* d = (ufs2_direct_t*)(chunk + pos);
    if (prev == ~0u) {
        d->d_ino = 0;                                // first in its chunk: blank it
    } else {
        ((ufs2_direct_t*)(chunk + prev))->d_reclen += d->d_reclen;   // fold into the one before
    }
    i64 w = write_data(u, dino, &dir, base, chunk, UFS2_DIRBLKSIZ);
    return w < 0 ? (i32)w : 0;
}

// Point an existing entry at another inode (rename over a file, "..").
static i32 dir_retarget(ufs_t* u, u32 dino, const char* name, u32 len, u32 ino, u8 type) {
    ufs2_dinode_t dir;
    if (!read_inode(u, dino, &dir)) return -EIO;
    u8 chunk[UFS2_DIRBLKSIZ];
    u32 old, pos;
    u64 base;
    i32 r = dir_find(u, &dir, name, len, &old, &base, &pos, NULL, chunk);
    if (r < 0) return r;
    ufs2_direct_t* d = (ufs2_direct_t*)(chunk + pos);
    d->d_ino = ino;
    d->d_type = type;
    i64 w = write_data(u, dino, &dir, base, chunk, UFS2_DIRBLKSIZ);
    return w < 0 ? (i32)w : 0;
}

static bool count_visit(const char* name, u32 namlen, u32 ino, void* ctx) {
    (void)ino;
    bool dot = (namlen == 1 && name[0] == '.') || (namlen == 2 && name[0] == '.' && name[1] == '.');
    if (!dot) (*(u32*)ctx)++;
    return false;
}

static u8 dtype_of(u16 mode) {
    switch (mode & UFS2_IFMT) {
        case UFS2_IFDIR: return UFS2_DT_DIR;
        case UFS2_IFREG: return UFS2_DT_REG;
        case UFS2_IFLNK: return UFS2_DT_LNK;
        case UFS2_IFCHR: return UFS2_DT_CHR;
        case UFS2_IFIFO: return UFS2_DT_FIFO;
        case UFS2_IFSOCK: return UFS2_DT_SOCK;
        default:         return UFS2_DT_UNKNOWN;
    }
}

// ---------------------------------------------------------------------------
// VFS interface. A vnode's id is its inode number.
// ---------------------------------------------------------------------------

static void fill_node(u32 ino, const ufs2_dinode_t* ip, vnode_t* out) {
    u32 fmt = ip->di_mode & UFS2_IFMT;
    out->id = ino;
    out->aux = 0;
    out->size = ip->di_size;
    out->mtime = ip->di_mtime > 0 ? (u64)ip->di_mtime : 0;
    out->atime = ip->di_atime > 0 ? (u64)ip->di_atime : 0;
    out->ctime = ip->di_ctime > 0 ? (u64)ip->di_ctime : 0;
    out->type = fmt == UFS2_IFDIR ? VN_DIR : fmt == UFS2_IFREG ? VN_REG :
                fmt == UFS2_IFLNK ? VN_LNK : fmt == UFS2_IFIFO ? VN_FIFO :
                fmt == UFS2_IFSOCK ? VN_SOCK : VN_CHR;
    // 07777 permissions; a file with none at all still needs to look
    // "known", so mode 0 is reported as such only by filesystems without.
    out->mode = ip->di_mode & 07777;
    if (!out->mode) out->mode = 01000000;            // (masked off again by stat)
    out->uid = ip->di_uid;
    out->gid = ip->di_gid;
    out->nlink = ip->di_nlink > 0 ? (u32)ip->di_nlink : 0;
}

static i32 node_from_inode(ufs_t* u, u32 ino, vnode_t* out) {
    ufs2_dinode_t ip;
    if (!read_inode(u, ino, &ip)) return -EIO;
    fill_node(ino, &ip, out);
    return 0;
}

static i32 ufs2_root(vnode_t* out) {
    U(out)->self = out->mnt;
    return node_from_inode(U(out), UFS2_ROOT_INO, out);
}

typedef struct {
    const char* want;
    u32 want_len;
    u32 found;
} lookup_ctx_t;

static bool lookup_visit(const char* name, u32 namlen, u32 ino, void* ctx) {
    lookup_ctx_t* l = (lookup_ctx_t*)ctx;
    if (namlen != l->want_len) return false;
    for (u32 i = 0; i < namlen; i++) if (name[i] != l->want[i]) return false;
    l->found = ino;
    return true;
}

static i32 ufs2_lookup(const vnode_t* dir, const char* name, vnode_t* out) {
    ufs_t* u = U(dir);
    ufs2_dinode_t ip;
    if (!read_inode(u, (u32)dir->id, &ip)) return -EIO;
    if ((ip.di_mode & UFS2_IFMT) != UFS2_IFDIR) return -ENOTDIR;
    lookup_ctx_t l = { name, (u32)strlen(name), 0 };
    if (!dir_walk(u, &ip, lookup_visit, &l)) return -EIO;
    if (!l.found) return -ENOENT;
    return node_from_inode(u, l.found, out);
}

static i64 ufs2_read(vnode_t* node, u64 off, void* buf, u64 len) {
    ufs_t* u = U(node);
    ufs2_dinode_t ip;
    if (!read_inode(u, (u32)node->id, &ip)) return -EIO;
    if (off >= ip.di_size) return 0;
    if (len > ip.di_size - off) len = ip.di_size - off;
    if (!read_data(u, &ip, off, buf, len)) return -EIO;
    return (i64)len;
}

static i64 ufs2_write(vnode_t* node, u64 off, const void* buf, u64 len) {
    ufs_t* u = U(node);
    i32 r = begin_write(u);
    if (r < 0) return r;
    ufs2_dinode_t ip;
    if (!read_inode(u, (u32)node->id, &ip)) return -EIO;
    if ((ip.di_mode & UFS2_IFMT) == UFS2_IFDIR) return -EISDIR;
    i64 n = write_data(u, (u32)node->id, &ip, off, buf, len);
    r = commit(u);
    node->size = ip.di_size;
    node->mtime = (u64)ip.di_mtime;
    return n < 0 ? n : (r < 0 ? r : n);
}

static i32 ufs2_truncate(vnode_t* node, u64 size) {
    ufs_t* u = U(node);
    i32 r = begin_write(u);
    if (r < 0) return r;
    ufs2_dinode_t ip;
    if (!read_inode(u, (u32)node->id, &ip)) return -EIO;
    r = truncate_inode(u, (u32)node->id, &ip, size);
    i32 c = commit(u);
    node->size = ip.di_size;
    node->mtime = (u64)ip.di_mtime;
    return r < 0 ? r : c;
}

typedef struct {
    u32 index;
    u32 seen;
    char* name_out;
    u32 ino;
} readdir_ctx_t;

static bool readdir_visit(const char* name, u32 namlen, u32 ino, void* ctx) {
    readdir_ctx_t* r = (readdir_ctx_t*)ctx;
    if ((namlen == 1 && name[0] == '.') ||
        (namlen == 2 && name[0] == '.' && name[1] == '.')) {
        return false;
    }
    if (r->seen++ != r->index) return false;
    u32 n = namlen < VFS_NAME_MAX - 1 ? namlen : VFS_NAME_MAX - 1;
    for (u32 i = 0; i < n; i++) r->name_out[i] = name[i];
    r->name_out[n] = '\0';
    r->ino = ino;
    return true;
}

static i32 ufs2_readdir(const vnode_t* dir, u32 index, char* name_out, vnode_t* out) {
    ufs_t* u = U(dir);
    ufs2_dinode_t ip;
    if (!read_inode(u, (u32)dir->id, &ip)) return -EIO;
    readdir_ctx_t r = { index, 0, name_out, 0 };
    if (!dir_walk(u, &ip, readdir_visit, &r)) return -EIO;
    if (!r.ino) return 0;
    i32 e = node_from_inode(u, r.ino, out);
    return e < 0 ? e : 1;
}

static i32 free_inode(ufs_t* u, u32 ino, ufs2_dinode_t* ip_in);

// Drop one link to an inode; free it with the last.
static i32 drop_link(ufs_t* u, u32 ino) {
    ufs2_dinode_t ip;
    if (!read_inode(u, ino, &ip)) return -EIO;
    bool dir = (ip.di_mode & UFS2_IFMT) == UFS2_IFDIR;
    // A directory's links are its entry in the parent and its own "."; it
    // goes when the entry does.
    ip.di_nlink = dir ? 0 : (i16)(ip.di_nlink - 1);
    if (ip.di_nlink > 0) {
        touch(&ip, false);
        return write_inode(u, ino, &ip) ? 0 : -EIO;
    }
    // Still open somewhere: it lives on, nameless, until the last close
    // (ufs2_release). Should the machine stop first, fsck reclaims it.
    vnode_t probe = { .mnt = u->self, .id = ino };
    if (vnode_is_open(&probe)) {
        touch(&ip, false);
        return write_inode(u, ino, &ip) ? 0 : -EIO;
    }
    return free_inode(u, ino, &ip);
}

// Give an inode with no links left, and everything it holds, back.
static i32 free_inode(ufs_t* u, u32 ino, ufs2_dinode_t* ip_in) {
    ufs2_dinode_t ip = *ip_in;
    bool dir = (ip.di_mode & UFS2_IFMT) == UFS2_IFDIR;
    i32 r = truncate_inode(u, ino, &ip, 0);
    if (r < 0) return r;
    u32 gen = ip.di_gen;
    memset(&ip, 0, sizeof(ip));
    ip.di_gen = gen + 1;                             // fsck wants mode, size, blocks zero
    if (!write_inode(u, ino, &ip)) return -EIO;
    inode_free(u, ino, dir);
    return 0;
}

static i32 adjust_nlink(ufs_t* u, u32 ino, i32 delta) {
    ufs2_dinode_t ip;
    if (!read_inode(u, ino, &ip)) return -EIO;
    ip.di_nlink = (i16)(ip.di_nlink + delta);
    touch(&ip, false);
    return write_inode(u, ino, &ip) ? 0 : -EIO;
}

static i32 ufs2_create(const vnode_t* dirnode, const char* name, u32 type, u32 mode, vnode_t* out) {
    ufs_t* u = U(dirnode);
    i32 r = begin_write(u);
    if (r < 0) return r;
    u32 len = (u32)strlen(name);
    if (len == 0 || len > 255) return -ENAMETOOLONG;
    u32 pino = (u32)dirnode->id;
    bool dir = type == VN_DIR;

    u32 ino = inode_alloc(u, cg_of_ino(u, pino), dir);
    if (!ino) { commit(u); return -ENOSPC; }
    ufs2_dinode_t ip;
    memset(&ip, 0, sizeof(ip));
    u32 fmt = dir ? UFS2_IFDIR : type == VN_SOCK ? UFS2_IFSOCK : UFS2_IFREG;
    ip.di_mode = (u16)(fmt | (mode & 07777));
    ip.di_nlink = dir ? 2 : 1;
    ip.di_gen = (u32)weak_random() | 1;
    i64 now = (i64)rtc_now();
    ip.di_atime = ip.di_mtime = ip.di_ctime = ip.di_birthtime = now;
    if (!write_inode(u, ino, &ip)) { r = -EIO; goto undo; }

    if (dir) {
        // One chunk: "." (just big enough) and ".." taking the rest.
        u8 chunk[UFS2_DIRBLKSIZ];
        memset(chunk, 0, sizeof(chunk));
        put_entry(chunk, ino, (u16)UFS2_DIRSIZ(1), UFS2_DT_DIR, ".", 1);
        put_entry(chunk + UFS2_DIRSIZ(1), pino, (u16)(UFS2_DIRBLKSIZ - UFS2_DIRSIZ(1)), UFS2_DT_DIR, "..", 2);
        i64 w = write_data(u, ino, &ip, 0, chunk, UFS2_DIRBLKSIZ);
        if (w < 0) { r = (i32)w; goto undo; }
    }
    r = dir_add(u, pino, name, len, ino, dtype_of(ip.di_mode));
    if (r < 0) goto undo;
    if (dir && (r = adjust_nlink(u, pino, 1)) < 0) goto out;

    r = node_from_inode(u, ino, out);
out:
    {
        i32 c = commit(u);
        return r < 0 ? r : c;
    }
undo:
    // Nothing points at the inode yet: just give it (and any blocks) back.
    if (read_inode(u, ino, &ip)) {
        truncate_inode(u, ino, &ip, 0);
        memset(&ip, 0, sizeof(ip));
        write_inode(u, ino, &ip);
    }
    inode_free(u, ino, dir);
    commit(u);
    return r;
}

static i32 ufs2_unlink(const vnode_t* dirnode, const char* name) {
    ufs_t* u = U(dirnode);
    i32 r = begin_write(u);
    if (r < 0) return r;
    u32 pino = (u32)dirnode->id, len = (u32)strlen(name);
    ufs2_dinode_t dir, ip;
    u8 chunk[UFS2_DIRBLKSIZ];
    u32 ino;
    if (!read_inode(u, pino, &dir)) return -EIO;
    r = dir_find(u, &dir, name, len, &ino, NULL, NULL, NULL, chunk);
    if (r < 0) return r;
    if (!read_inode(u, ino, &ip)) return -EIO;
    bool is_dir = (ip.di_mode & UFS2_IFMT) == UFS2_IFDIR;
    if (is_dir) {
        u32 entries = 0;
        if (!dir_walk(u, &ip, count_visit, &entries)) return -EIO;
        if (entries) return -ENOTEMPTY;
    }
    r = dir_remove(u, pino, name, len);
    if (r == 0) r = drop_link(u, ino);
    if (r == 0 && is_dir) r = adjust_nlink(u, pino, -1);   // its ".." is gone
    i32 c = commit(u);
    return r < 0 ? r : c;
}

// Is inode `ino` the directory `anc` or inside it? (Walks ".." to the root.)
static i32 is_within(ufs_t* u, u32 ino, u32 anc) {
    for (u32 depth = 0; depth < 4096; depth++) {
        if (ino == anc) return 1;
        if (ino == UFS2_ROOT_INO) return 0;
        ufs2_dinode_t ip;
        u8 chunk[UFS2_DIRBLKSIZ];
        u32 parent;
        if (!read_inode(u, ino, &ip)) return -EIO;
        if (dir_find(u, &ip, "..", 2, &parent, NULL, NULL, NULL, chunk) < 0) return -EIO;
        ino = parent;
    }
    return -EIO;                                     // a cycle: corrupt
}

static i32 ufs2_rename(const vnode_t* olddir, const char* oldname,
                       const vnode_t* newdir, const char* newname) {
    ufs_t* u = U(olddir);
    i32 r = begin_write(u);
    if (r < 0) return r;
    u32 op = (u32)olddir->id, np = (u32)newdir->id;
    u32 olen = (u32)strlen(oldname), nlen = (u32)strlen(newname);
    if (nlen == 0 || nlen > 255) return -ENAMETOOLONG;
    ufs2_dinode_t dir, ip;
    u8 chunk[UFS2_DIRBLKSIZ];
    u32 ino, existing = 0;
    if (!read_inode(u, op, &dir)) return -EIO;
    if ((r = dir_find(u, &dir, oldname, olen, &ino, NULL, NULL, NULL, chunk)) < 0) return r;
    if (!read_inode(u, ino, &ip)) return -EIO;
    bool is_dir = (ip.di_mode & UFS2_IFMT) == UFS2_IFDIR;
    u8 type = dtype_of(ip.di_mode);

    if (is_dir && op != np) {
        i32 w = is_within(u, np, ino);
        if (w < 0) return w;
        if (w) return -EINVAL;                       // into itself
    }
    if (!read_inode(u, np, &dir)) return -EIO;
    if (dir_find(u, &dir, newname, nlen, &existing, NULL, NULL, NULL, chunk) == 0) {
        if (existing == ino) return 0;
        // (The VFS has already refused to replace a directory.)
        r = dir_retarget(u, np, newname, nlen, ino, type);
        if (r == 0) r = drop_link(u, existing);
    } else {
        r = dir_add(u, np, newname, nlen, ino, type);
    }
    if (r == 0) r = dir_remove(u, op, oldname, olen);
    if (r == 0 && is_dir && op != np) {
        r = dir_retarget(u, ino, "..", 2, np, UFS2_DT_DIR);
        if (r == 0) r = adjust_nlink(u, op, -1);
        if (r == 0) r = adjust_nlink(u, np, 1);
    }
    if (r == 0) {
        if (!read_inode(u, ino, &ip)) r = -EIO;
        else { touch(&ip, false); if (!write_inode(u, ino, &ip)) r = -EIO; }
    }
    i32 c = commit(u);
    return r < 0 ? r : c;
}

static i64 ufs2_readlink(vnode_t* node, char* buf, u64 len) {
    ufs_t* u = U(node);
    ufs2_dinode_t ip;
    if (!read_inode(u, (u32)node->id, &ip)) return -EIO;
    if ((ip.di_mode & UFS2_IFMT) != UFS2_IFLNK) return -EINVAL;
    if (len > ip.di_size) len = ip.di_size;
    if (fast_link(u, &ip)) {
        memcpy(buf, (const u8*)ip.di_db, len);
        return (i64)len;
    }
    return read_data(u, &ip, 0, buf, len) ? (i64)len : -EIO;
}

static i32 ufs2_symlink(const vnode_t* dirnode, const char* name, const char* target, vnode_t* out) {
    ufs_t* u = U(dirnode);
    i32 r = begin_write(u);
    if (r < 0) return r;
    u32 len = (u32)strlen(name), tlen = (u32)strlen(target);
    if (len == 0 || len > 255) return -ENAMETOOLONG;
    u32 pino = (u32)dirnode->id;
    u32 ino = inode_alloc(u, cg_of_ino(u, pino), false);
    if (!ino) { commit(u); return -ENOSPC; }
    ufs2_dinode_t ip;
    memset(&ip, 0, sizeof(ip));
    ip.di_mode = UFS2_IFLNK | 0777;
    ip.di_nlink = 1;
    ip.di_gen = (u32)weak_random() | 1;
    i64 now = (i64)rtc_now();
    ip.di_atime = ip.di_mtime = ip.di_ctime = ip.di_birthtime = now;
    if (tlen < (u32)u->fs.fs_maxsymlinklen) {
        memcpy((u8*)ip.di_db, target, tlen);            // a fast link: no blocks
        ip.di_size = tlen;
        r = write_inode(u, ino, &ip) ? 0 : -EIO;
    } else {
        r = write_inode(u, ino, &ip) ? 0 : -EIO;
        if (r == 0) {
            i64 w = write_data(u, ino, &ip, 0, target, tlen);
            if (w < 0) r = (i32)w;
        }
    }
    if (r == 0) r = dir_add(u, pino, name, len, ino, UFS2_DT_LNK);
    if (r < 0) {
        if (read_inode(u, ino, &ip)) free_inode(u, ino, &ip);
        commit(u);
        return r;
    }
    r = node_from_inode(u, ino, out);
    i32 c = commit(u);
    return r < 0 ? r : c;
}

static i32 ufs2_link(const vnode_t* dirnode, const char* name, const vnode_t* target) {
    ufs_t* u = U(dirnode);
    i32 r = begin_write(u);
    if (r < 0) return r;
    u32 len = (u32)strlen(name), ino = (u32)target->id;
    if (len == 0 || len > 255) return -ENAMETOOLONG;
    ufs2_dinode_t ip;
    if (!read_inode(u, ino, &ip)) return -EIO;
    if ((ip.di_mode & UFS2_IFMT) == UFS2_IFDIR) return -EPERM;
    if (ip.di_nlink >= 32767) return -EMLINK;
    ip.di_nlink++;
    touch(&ip, false);
    if (!write_inode(u, ino, &ip)) return -EIO;
    r = dir_add(u, (u32)dirnode->id, name, len, ino, dtype_of(ip.di_mode));
    if (r < 0) adjust_nlink(u, ino, -1);
    i32 c = commit(u);
    return r < 0 ? r : c;
}

static i32 ufs2_setattr(vnode_t* node, u32 mask, const vattr_t* a) {
    ufs_t* u = U(node);
    i32 r = begin_write(u);
    if (r < 0) return r;
    u32 ino = (u32)node->id;
    ufs2_dinode_t ip;
    if (!read_inode(u, ino, &ip)) return -EIO;
    if (mask & VATTR_MODE) ip.di_mode = (u16)((ip.di_mode & UFS2_IFMT) | (a->mode & 07777));
    if (mask & VATTR_UID) ip.di_uid = a->uid;
    if (mask & VATTR_GID) ip.di_gid = a->gid;
    if (mask & VATTR_ATIME) { ip.di_atime = (i64)a->atime; ip.di_atimensec = 0; }
    if (mask & VATTR_MTIME) { ip.di_mtime = (i64)a->mtime; ip.di_mtimensec = 0; }
    touch(&ip, false);
    if (!write_inode(u, ino, &ip)) return -EIO;
    fill_node(ino, &ip, node);
    return commit(u);
}

static i32 ufs2_statfs(const struct mount* m, vfs_statfs_t* out) {
    ufs_t* u = (ufs_t*)m->priv;
    const ufs2_fs_t* fs = &u->fs;
    u64 free = (u64)fs->fs_cstotal.cs_nbfree * u->frag + (u64)fs->fs_cstotal.cs_nffree;
    u64 reserve = (u64)fs->fs_dsize * (u64)fs->fs_minfree / 100;
    out->bsize = u->fsize;
    out->blocks = (u64)fs->fs_dsize;
    out->bfree = free;
    out->bavail = free > reserve ? free - reserve : 0;
    out->files = (u64)u->ncg * u->ipg;
    out->ffree = (u64)fs->fs_cstotal.cs_nifree;
    out->namemax = 255;
    return 0;
}

static i32 ufs2_getattr(vnode_t* node) {
    return node_from_inode(U(node), (u32)node->id, node);
}

// The last open of an inode closed: if its last link went meanwhile, now
// it can go too.
static void ufs2_release(vnode_t* node) {
    ufs_t* u = U(node);
    ufs2_dinode_t ip;
    if (!read_inode(u, (u32)node->id, &ip) || ip.di_mode == 0 || ip.di_nlink > 0) return;
    if (begin_write(u) < 0) return;
    free_inode(u, (u32)node->id, &ip);
    commit(u);
}

// Going down: nothing is in flight between operations, so the filesystem
// is consistent -- say so.
static void ufs2_shutdown(const struct mount* m) {
    ufs_t* u = (ufs_t*)m->priv;
    if (!u->writable || u->fs.fs_clean) return;
    u->fs.fs_clean = 1;
    u->sb_dirty = true;
    if (commit(u) == 0) klog(LOG_OKAY, "ufs2: drive %u marked clean\n", u->drive);
}

const vfs_ops_t ufs2_vfs_ops = {
    .type = "ufs2",
    .root = ufs2_root,
    .lookup = ufs2_lookup,
    .read = ufs2_read,
    .write = ufs2_write,
    .readdir = ufs2_readdir,
    .create = ufs2_create,
    .unlink = ufs2_unlink,
    .truncate = ufs2_truncate,
    .rename = ufs2_rename,
    .readlink = ufs2_readlink,
    .symlink = ufs2_symlink,
    .link = ufs2_link,
    .setattr = ufs2_setattr,
    .statfs = ufs2_statfs,
    .getattr = ufs2_getattr,
    .release = ufs2_release,
    .shutdown = ufs2_shutdown,
    .keeps_unlinked = true,
};

// ---------------------------------------------------------------------------
// Mounting
// ---------------------------------------------------------------------------

static bool is_pow2(u32 v) {
    return v && !(v & (v - 1));
}

void* ufs2_mount(u8 drive) {
    ufs_t* u = kmalloc(sizeof(ufs_t));
    if (!u) return NULL;
    memset(u, 0, sizeof(*u));
    u->drive = drive;
    u->sec_lba = ~0ULL;
    u->cg_loaded = -1;

    static const u32 search[] = UFS2_SBLOCK_SEARCH;
    bool found = false;
    for (u32 i = 0; i < sizeof(search) / sizeof(search[0]); i++) {
        if (dev_read(u, search[i], &u->fs, sizeof(u->fs)) && u->fs.fs_magic == UFS2_MAGIC) {
            u->sbloc = search[i];
            found = true;
            break;
        }
    }
    if (!found) {
        klog(LOG_WARN, "ufs2: no UFS2 superblock on drive %u\n", drive);
        kfree(u);
        return NULL;
    }
    ufs2_fs_t* fs = &u->fs;
    u->bsize = (u32)fs->fs_bsize;
    u->fsize = (u32)fs->fs_fsize;
    u->frag = (u32)fs->fs_frag;
    u->frag_shift = (u32)fs->fs_fragshift;
    u->bshift = (u32)fs->fs_bshift;
    u->ncg = fs->fs_ncg;
    u->ipg = fs->fs_ipg;
    u->fpg = (u32)fs->fs_fpg;
    u->inopb = fs->fs_inopb;
    u->nindir = (u32)fs->fs_nindir;
    u->nfrags = (u64)fs->fs_size;

    // Sanity-check the geometry before trusting any of it for arithmetic.
    if (!is_pow2(u->bsize) || u->bsize < 4096 || u->bsize > 65536 ||
        !is_pow2(u->fsize) || u->fsize < 512 || u->fsize > u->bsize ||
        u->frag != u->bsize / u->fsize || u->frag > UFS2_MAXFRAG || (1U << u->frag_shift) != u->frag ||
        (1U << u->bshift) != u->bsize ||
        u->inopb != u->bsize / sizeof(ufs2_dinode_t) || u->nindir != u->bsize / 8 ||
        u->ncg == 0 || u->ipg == 0 || u->fpg == 0 || u->ipg % u->inopb ||
        u->nfrags > (u64)u->ncg * u->fpg || fs->fs_cgsize <= 0 || (u32)fs->fs_cgsize > u->bsize ||
        fs->fs_sbsize < (i32)sizeof(ufs2_fs_t) || fs->fs_sbsize > 8192 ||
        (u64)fs->fs_cssize < (u64)u->ncg * sizeof(ufs2_csum_t) || fs->fs_cssize > (1 << 20) ||
        !frag_valid(u, (u64)fs->fs_csaddr)) {
        klog(LOG_ERROR, "ufs2: superblock geometry is inconsistent\n");
        kfree(u);
        return NULL;
    }

    u->sbbuf = kmalloc((u64)fs->fs_sbsize);
    u->csum_bytes = (u32)((fs->fs_cssize + u->fsize - 1) / u->fsize * u->fsize);
    u->csum = kmalloc(u->csum_bytes);
    u->cgbuf = kmalloc(u->bsize);
    if (!u->sbbuf || !u->csum || !u->cgbuf ||
        !dev_read(u, u->sbloc, u->sbbuf, (u64)fs->fs_sbsize) ||
        !dev_read(u, frag_byte(u, (u64)fs->fs_csaddr), u->csum, u->csum_bytes)) {
        klog(LOG_ERROR, "ufs2: cannot read the superblock and summaries\n");
        if (u->sbbuf) kfree(u->sbbuf);
        if (u->csum) kfree(u->csum);
        if (u->cgbuf) kfree(u->cgbuf);
        kfree(u);
        return NULL;
    }

    // Writing is safe without soft updates and without a journal -- unless
    // the filesystem expects them to be kept up (SUJ), or snapshots exist.
    u->writable = !(fs->fs_flags & 0x0008) && fs->fs_snapinum[0] == 0;
    if (!fs->fs_clean) klog(LOG_WARN, "ufs2: drive %u was not cleanly unmounted; run fsck_ffs\n", drive);
    klog(LOG_OKAY, "ufs2: drive %u: %u groups, %u-byte blocks, %u-byte fragments, %u inodes, %s\n",
         drive, u->ncg, u->bsize, u->fsize, u->ncg * u->ipg, u->writable ? "read-write" : "read-only");
    return u;
}
