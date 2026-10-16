// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>
#include <vfs.h>

// UFS2 (the FreeBSD/NetBSD "Fast File System", version 2), read-write.
//
// On-disk vocabulary, since UFS has two sizes of everything:
//   block     fs_bsize bytes (e.g. 32 KiB) -- the unit files are allocated in
//   fragment  fs_fsize bytes (e.g. 4 KiB)  -- the unit block *addresses*
//             count in; a file's tail may occupy just a few fragments
//   sector    512 bytes, what the disk reads; fsbtodb converts fragments
//
// The disk is divided into cylinder groups, each with its own slice of the
// inode table, a bitmap of its inodes and one of its fragments, and summary
// counts. Inode N lives in group N / fs_ipg.
//
// The structures below are FreeBSD's (sys/ufs/ffs/fs.h, sys/ufs/ufs/
// dinode.h, sys/ufs/ufs/dir.h), field for field; the offsets are checked.

// The superblock has no single home: FreeBSD's newfs puts UFS2's at 64 KiB,
// NetBSD's makefs at 8 KiB for small images. Like FreeBSD (SBLOCKSEARCH),
// probe each candidate and take the first with the right magic.
#define UFS2_SBLOCK_SEARCH { 65536, 8192, 0, 262144 }
#define UFS2_MAGIC         0x19540119
#define UFS2_CG_MAGIC      0x090255
#define UFS2_ROOT_INO      2
#define UFS2_NDADDR        12             // direct block pointers per inode
#define UFS2_NIADDR        3              // single, double, triple indirect
#define UFS2_DIRBLKSIZ     512            // directory entries never cross these
#define UFS2_MAXFRAG       8
#define UFS2_MAXSYMLINKLEN ((UFS2_NDADDR + UFS2_NIADDR) * 8)   // stored in the inode

typedef struct {
    i32 cs_ndir;
    i32 cs_nbfree;
    i32 cs_nifree;
    i32 cs_nffree;
} ufs2_csum_t;

typedef struct {
    i64 cs_ndir;
    i64 cs_nbfree;
    i64 cs_nifree;
    i64 cs_nffree;
    i64 cs_numclusters;
    i64 cs_spare[3];
} ufs2_csum_total_t;

typedef struct {
    i32 fs_firstfield, fs_unused_1;
    i32 fs_sblkno, fs_cblkno, fs_iblkno, fs_dblkno;
    i32 fs_old_cgoffset, fs_old_cgmask, fs_old_time, fs_old_size, fs_old_dsize;
    u32 fs_ncg;
    i32 fs_bsize, fs_fsize, fs_frag;
    i32 fs_minfree, fs_old_rotdelay, fs_old_rps;
    i32 fs_bmask, fs_fmask, fs_bshift, fs_fshift;
    i32 fs_maxcontig, fs_maxbpg;
    i32 fs_fragshift, fs_fsbtodb, fs_sbsize;
    i32 fs_spare1[2];
    i32 fs_nindir;
    u32 fs_inopb;
    i32 fs_old_nspf;
    i32 fs_optim, fs_old_npsect, fs_old_interleave, fs_old_trackskew;
    i32 fs_id[2];
    i32 fs_old_csaddr, fs_cssize, fs_cgsize;
    i32 fs_spare2, fs_old_nsect, fs_old_spc, fs_old_ncyl, fs_old_cpg;
    u32 fs_ipg;
    i32 fs_fpg;
    ufs2_csum_t fs_old_cstotal;
    i8  fs_fmod, fs_clean, fs_ronly, fs_old_flags;
    u8  fs_fsmnt[468];
    u8  fs_volname[32];
    u64 fs_swuid;
    i32 fs_pad;
    i32 fs_cgrotor;
    u8  fs_incore_ptrs[128];      // pointers FreeBSD keeps in core; 0 on disk
    i32 fs_old_cpc, fs_maxbsize;
    i64 fs_unrefs, fs_providersize, fs_metaspace;
    i64 fs_sparecon64[14];
    i64 fs_sblockloc;
    ufs2_csum_total_t fs_cstotal;
    i64 fs_time, fs_size, fs_dsize, fs_csaddr, fs_pendingblocks;
    u32 fs_pendinginodes;
    u32 fs_snapinum[20];
    u32 fs_avgfilesize, fs_avgfpdir;
    i32 fs_save_cgsize;
    i64 fs_mtime;
    i32 fs_sujfree;
    i32 fs_sparecon32[23];
    i32 fs_flags, fs_contigsumsize, fs_maxsymlinklen, fs_old_inodefmt;
    u64 fs_maxfilesize;
    i64 fs_qbmask, fs_qfmask;
    i32 fs_state, fs_old_postblformat, fs_old_nrpos;
    i32 fs_spare5[2];
    i32 fs_magic;
} ufs2_fs_t;

_Static_assert(sizeof(ufs2_fs_t) == 1376, "struct fs is 1376 bytes");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_ipg) == 184, "fs_ipg");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_clean) == 209, "fs_clean");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_cgrotor) == 724, "fs_cgrotor");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_old_cpc) == 856, "fs_old_cpc");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_sblockloc) == 1000, "fs_sblockloc");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_cstotal) == 1008, "fs_cstotal");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_csaddr) == 1096, "fs_csaddr");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_avgfilesize) == 1196, "fs_avgfilesize");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_flags) == 1312, "fs_flags");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_maxfilesize) == 1328, "fs_maxfilesize");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_magic) == 1372, "fs_magic");

// Cylinder group header; its maps follow at the offsets it records.
typedef struct {
    i32 cg_firstfield, cg_magic, cg_old_time;
    u32 cg_cgx;
    i16 cg_old_ncyl, cg_old_niblk;
    u32 cg_ndblk;                 // fragments in this group
    ufs2_csum_t cg_cs;
    u32 cg_rotor, cg_frotor, cg_irotor;
    u32 cg_frsum[UFS2_MAXFRAG];   // [n]: free runs of exactly n fragments
    i32 cg_old_btotoff, cg_old_boff;
    u32 cg_iusedoff;              // u8 bitmap: inode in use
    u32 cg_freeoff;               // u8 bitmap: fragment free
    u32 cg_nextfreeoff;
    u32 cg_clustersumoff;         // u32 counts of free block runs, by length
    u32 cg_clusteroff;            // u8 bitmap: block entirely free
    u32 cg_nclusterblks, cg_niblk, cg_initediblk, cg_unrefs;
    i32 cg_sparecon32[2];
    i64 cg_time;
    i64 cg_sparecon64[3];
} ufs2_cg_t;

_Static_assert(sizeof(ufs2_cg_t) == 168, "cg header is 168 bytes");
_Static_assert(__builtin_offsetof(ufs2_cg_t, cg_frsum) == 52, "cg_frsum");
_Static_assert(__builtin_offsetof(ufs2_cg_t, cg_iusedoff) == 92, "cg_iusedoff");
_Static_assert(__builtin_offsetof(ufs2_cg_t, cg_initediblk) == 120, "cg_initediblk");
_Static_assert(__builtin_offsetof(ufs2_cg_t, cg_time) == 136, "cg_time");

// UFS2 on-disk inode, 256 bytes.
typedef struct PACKED {
    u16 di_mode;
    i16 di_nlink;
    u32 di_uid;
    u32 di_gid;
    u32 di_blksize;
    u64 di_size;
    u64 di_blocks;            // 512-byte sectors held, indirect blocks included
    i64 di_atime;
    i64 di_mtime;
    i64 di_ctime;
    i64 di_birthtime;
    i32 di_mtimensec;
    i32 di_atimensec;
    i32 di_ctimensec;
    i32 di_birthnsec;
    u32 di_gen;
    u32 di_kernflags;
    u32 di_flags;
    u32 di_extsize;
    i64 di_extb[2];
    i64 di_db[UFS2_NDADDR];   // direct blocks, as fragment addresses
    i64 di_ib[UFS2_NIADDR];   // indirect blocks
    u64 di_modrev;
    u32 di_freelink;
    u32 di_ckhash;
    u32 di_spare[2];
} ufs2_dinode_t;

_Static_assert(sizeof(ufs2_dinode_t) == 256, "ufs2_dinode_t must be 256 bytes");

#define UFS2_IFMT  0170000
#define UFS2_IFIFO 0010000
#define UFS2_IFCHR 0020000
#define UFS2_IFDIR 0040000
#define UFS2_IFBLK 0060000
#define UFS2_IFREG 0100000
#define UFS2_IFLNK 0120000

// Directory entry header; the name (d_namlen bytes, then a NUL) follows, and
// d_reclen covers the whole record including padding.
typedef struct PACKED {
    u32 d_ino;                // 0 = unused slot
    u16 d_reclen;
    u8  d_type;
    u8  d_namlen;
} ufs2_direct_t;

#define UFS2_DT_UNKNOWN 0
#define UFS2_DT_FIFO    1
#define UFS2_DT_CHR     2
#define UFS2_DT_DIR     4
#define UFS2_DT_REG     8
#define UFS2_DT_LNK     10

// The space a directory entry with an n-byte name needs (DIRSIZ).
#define UFS2_DIRSIZ(n) ((sizeof(ufs2_direct_t) + (n) + 1 + 3) & ~3u)

// Look for a UFS2 filesystem on an ATA drive (0/1 primary master/slave,
// 2/3 secondary master/slave). Returns the mount's state, to hand to
// vfs_mount_with(..., &ufs2_vfs_ops, state), or NULL.
void* ufs2_mount(u8 drive);

extern const vfs_ops_t ufs2_vfs_ops;
