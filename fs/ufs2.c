// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// Read-only UFS2 driver. See include/ufs2.h for the on-disk vocabulary.
//
// Everything on the disk is treated as untrusted: block addresses, sizes and
// inode numbers are range-checked before use, so a corrupt image produces
// errors rather than wild reads.

#include <types.h>
#include <ufs2.h>
#include <vfs.h>
#include <ata.h>
#include <klog.h>
#include <memcpy.h>
#include <memset.h>
#include <errno.h>

// Geometry, cached from the superblock at mount.
static u8  ufs_drive;
static u32 bsize;          // bytes per block
static u32 fsize;          // bytes per fragment
static u32 frag_shift;     // log2(fragments per block)
static u32 iblkno;         // inode table offset within a group, in fragments
static u32 ncg;            // cylinder groups
static u32 ipg;            // inodes per group
static u64 fpg;            // fragments per group
static u32 inopb;          // inodes per block
static u32 nindir;         // block pointers per indirect block
static bool mounted = false;

// ---------------------------------------------------------------------------
// Raw access
// ---------------------------------------------------------------------------

// One-sector cache: directory scans and inode reads hit the same sector over
// and over, and every miss is a polled PIO transfer.
static u8  cache_buf[512];
static u64 cache_lba = ~0ULL;

static bool read_bytes(u64 offset, void* dst, u32 len) {
    u8* out = (u8*)dst;
    while (len) {
        u64 lba = offset / 512;
        u32 off = (u32)(offset % 512);
        if (lba != cache_lba) {
            if (lba > 0xFFFFFFFULL || !ata_read(ufs_drive, (u32)lba, cache_buf)) {
                cache_lba = ~0ULL;
                return false;
            }
            cache_lba = lba;
        }
        u32 n = 512 - off;
        if (n > len) n = len;
        memcpy(out, cache_buf + off, n);
        out += n;
        offset += n;
        len -= n;
    }
    return true;
}

static bool frag_valid(u64 frag) {
    return frag < (u64)ncg * fpg;
}

static bool read_inode(u32 ino, ufs2_dinode_t* out) {
    if (ino == 0 || ino >= ncg * ipg) return false;
    u32 cg = ino / ipg;
    u32 idx = ino % ipg;
    // The group's inode table starts iblkno fragments into the group, and
    // packs inopb inodes into each block (1 << frag_shift fragments).
    u64 frag = (u64)cg * fpg + iblkno + ((u64)(idx / inopb) << frag_shift);
    u64 byte = frag * fsize + (u64)(idx % inopb) * sizeof(ufs2_dinode_t);
    return read_bytes(byte, out, sizeof(*out));
}

// Fragment address holding logical block lbn of a file, 0 for a hole, or
// ~0 on error.
static u64 block_map(const ufs2_dinode_t* ip, u64 lbn) {
    if (lbn < UFS2_NDADDR) return (u64)ip->di_db[lbn];
    lbn -= UFS2_NDADDR;

    // Walk down the indirect tree: level 0 is single indirect (nindir
    // blocks), level 1 double (nindir^2), level 2 triple (nindir^3).
    u64 span = nindir;
    for (int level = 0; level < UFS2_NIADDR; level++) {
        if (lbn < span) {
            u64 blk = (u64)ip->di_ib[level];
            for (int depth = level; depth >= 0; depth--) {
                if (blk == 0) return 0;                       // hole
                if (!frag_valid(blk)) return ~0ULL;
                span /= nindir;                               // per-entry span
                u64 slot = lbn / span;
                lbn %= span;
                u64 next;
                if (!read_bytes(blk * fsize + slot * 8, &next, 8)) return ~0ULL;
                blk = next;
            }
            return blk;
        }
        lbn -= span;
        span *= nindir;
    }
    return ~0ULL;                                             // beyond triple
}

// Read len bytes of a file's data starting at off. Holes read as zeros.
static bool read_data(const ufs2_dinode_t* ip, u64 off, void* dst, u32 len) {
    u8* out = (u8*)dst;
    while (len) {
        u64 lbn = off / bsize;
        u32 boff = (u32)(off % bsize);
        u32 n = bsize - boff;
        if (n > len) n = len;

        u64 frag = block_map(ip, lbn);
        if (frag == ~0ULL) return false;
        if (frag == 0) {
            memset(out, 0, n);
        } else {
            if (!frag_valid(frag)) return false;
            if (!read_bytes(frag * fsize + boff, out, n)) return false;
        }
        out += n;
        off += n;
        len -= n;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Directories
// ---------------------------------------------------------------------------

// Call visit(name, namlen, ino, ctx) for every live entry in a directory;
// stops early when visit returns true. Returns false on a corrupt directory.
typedef bool (*dir_visit_t)(const char* name, u32 namlen, u32 ino, void* ctx);

static bool dir_walk(const ufs2_dinode_t* dir, dir_visit_t visit, void* ctx) {
    u8 chunk[UFS2_DIRBLKSIZ];
    for (u64 base = 0; base < dir->di_size; base += UFS2_DIRBLKSIZ) {
        if (!read_data(dir, base, chunk, UFS2_DIRBLKSIZ)) return false;
        u32 pos = 0;
        while (pos + sizeof(ufs2_direct_t) <= UFS2_DIRBLKSIZ) {
            ufs2_direct_t* d = (ufs2_direct_t*)(chunk + pos);
            if (d->d_reclen < sizeof(ufs2_direct_t) || pos + d->d_reclen > UFS2_DIRBLKSIZ) {
                return false;                                 // corrupt chain
            }
            if (d->d_ino && sizeof(ufs2_direct_t) + d->d_namlen <= d->d_reclen) {
                const char* name = (const char*)(d + 1);
                if (visit(name, d->d_namlen, d->d_ino, ctx)) return true;
            }
            pos += d->d_reclen;
        }
    }
    return true;
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

// ---------------------------------------------------------------------------
// VFS interface. A vnode's id is its inode number.
// ---------------------------------------------------------------------------

static i32 node_from_inode(u32 ino, vnode_t* out) {
    ufs2_dinode_t ip;
    if (!read_inode(ino, &ip)) return -EIO;
    u32 fmt = ip.di_mode & UFS2_IFMT;
    out->id = ino;
    out->aux = 0;
    out->size = ip.di_size;
    out->mtime = ip.di_mtime > 0 ? (u64)ip.di_mtime : 0;
    out->type = fmt == UFS2_IFDIR ? VN_DIR : fmt == UFS2_IFREG ? VN_REG : VN_CHR;
    return 0;
}

static i32 ufs2_root(vnode_t* out) {
    if (!mounted) return -ENODEV;
    return node_from_inode(UFS2_ROOT_INO, out);
}

static i32 ufs2_lookup(const vnode_t* dir, const char* name, vnode_t* out) {
    ufs2_dinode_t ip;
    if (!read_inode((u32)dir->id, &ip)) return -EIO;
    lookup_ctx_t l = { name, 0, 0 };
    while (name[l.want_len]) l.want_len++;
    if (!dir_walk(&ip, lookup_visit, &l)) return -EIO;
    if (!l.found) return -ENOENT;
    return node_from_inode(l.found, out);
}

static i64 ufs2_read(vnode_t* node, u64 off, void* buf, u64 len) {
    ufs2_dinode_t ip;
    if (!read_inode((u32)node->id, &ip)) return -EIO;
    if (off >= ip.di_size) return 0;
    if (len > ip.di_size - off) len = ip.di_size - off;
    if (len > 0x7FFFFFFF) len = 0x7FFFFFFF;
    if (!read_data(&ip, off, buf, (u32)len)) return -EIO;
    return (i64)len;
}

static i32 ufs2_readdir(const vnode_t* dir, u32 index, char* name_out, vnode_t* out) {
    ufs2_dinode_t ip;
    if (!read_inode((u32)dir->id, &ip)) return -EIO;
    readdir_ctx_t r = { index, 0, name_out, 0 };
    if (!dir_walk(&ip, readdir_visit, &r)) return -EIO;
    if (!r.ino) return 0;
    i32 e = node_from_inode(r.ino, out);
    return e < 0 ? e : 1;
}

const vfs_ops_t ufs2_vfs_ops = {
    .type = "ufs2",
    .root = ufs2_root,
    .lookup = ufs2_lookup,
    .read = ufs2_read,
    .readdir = ufs2_readdir,
    // Read-only for now: write, create, unlink and truncate stay NULL, which
    // the VFS reports as EROFS.
};

static u32 sb_u32(const u8* sb, u32 off) {
    return *(const u32*)(sb + off);
}

static bool is_pow2(u32 v) {
    return v && !(v & (v - 1));
}

bool ufs2_mount(u8 drive) {
    ufs_drive = drive;
    cache_lba = ~0ULL;

    static u8 sb[1376];
    static const u32 search[] = UFS2_SBLOCK_SEARCH;
    bool found = false;
    u32 sb_offset = 0;
    for (u32 i = 0; i < sizeof(search) / sizeof(search[0]); i++) {
        if (read_bytes(search[i], sb, sizeof(sb)) && sb_u32(sb, UFS2_SB_MAGIC) == UFS2_MAGIC) {
            found = true;
            sb_offset = search[i];
            break;
        }
    }
    if (!found) {
        klog(LOG_WARN, "ufs2: no UFS2 superblock on drive %u\n", drive);
        return false;
    }

    bsize      = sb_u32(sb, UFS2_SB_BSIZE);
    fsize      = sb_u32(sb, UFS2_SB_FSIZE);
    u32 frag   = sb_u32(sb, UFS2_SB_FRAG);
    frag_shift = sb_u32(sb, UFS2_SB_FRAGSHIFT);
    iblkno     = sb_u32(sb, UFS2_SB_IBLKNO);
    ncg        = sb_u32(sb, UFS2_SB_NCG);
    ipg        = sb_u32(sb, UFS2_SB_IPG);
    fpg        = sb_u32(sb, UFS2_SB_FPG);
    inopb      = sb_u32(sb, UFS2_SB_INOPB);
    nindir     = sb_u32(sb, UFS2_SB_NINDIR);

    // Sanity-check the geometry before trusting any of it for arithmetic.
    if (!is_pow2(bsize) || bsize < 4096 || bsize > 65536 ||
        !is_pow2(fsize) || fsize < 512 || fsize > bsize ||
        frag != bsize / fsize || (1U << frag_shift) != frag ||
        inopb != bsize / sizeof(ufs2_dinode_t) || nindir != bsize / 8 ||
        ncg == 0 || ipg == 0 || fpg == 0) {
        klog(LOG_ERROR, "ufs2: superblock geometry is inconsistent\n");
        return false;
    }

    mounted = true;
    klog(LOG_OKAY, "ufs2: drive %u: superblock at %u, %u groups, %u-byte blocks, %u-byte fragments, %u inodes\n",
         drive, sb_offset, ncg, bsize, fsize, ncg * ipg);
    return true;
}
