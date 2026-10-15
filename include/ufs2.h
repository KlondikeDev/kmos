// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>
#include <vfs.h>

// UFS2 (the FreeBSD/NetBSD "Fast File System", version 2), read-only.
//
// On-disk vocabulary, since UFS has two sizes of everything:
//   block     fs_bsize bytes (e.g. 32 KiB) -- the unit files are allocated in
//   fragment  fs_fsize bytes (e.g. 4 KiB)  -- the unit block *addresses*
//             count in; a file's tail may occupy just a few fragments
//   sector    512 bytes, what the disk reads; fsbtodb converts fragments
//
// The disk is divided into cylinder groups, each with its own slice of the
// inode table. Inode N lives in group N / fs_ipg.

// The superblock has no single home: FreeBSD's newfs puts UFS2's at 64 KiB,
// NetBSD's makefs at 8 KiB for small images. Like FreeBSD (SBLOCKSEARCH),
// probe each candidate and take the first with the right magic.
#define UFS2_SBLOCK_SEARCH { 65536, 8192, 0, 262144 }
#define UFS2_MAGIC         0x19540119
#define UFS2_ROOT_INO      2
#define UFS2_NDADDR        12             // direct block pointers per inode
#define UFS2_NIADDR        3              // single, double, triple indirect
#define UFS2_DIRBLKSIZ     512            // directory entries never cross these

// Superblock: only the fields this driver uses, at their FreeBSD offsets
// (sys/ufs/ffs/fs.h). The full structure is 1376 bytes.
#define UFS2_SB_SBLKNO    0x08
#define UFS2_SB_IBLKNO    0x10            // inode table offset within a group
#define UFS2_SB_NCG       0x2C
#define UFS2_SB_BSIZE     0x30
#define UFS2_SB_FSIZE     0x34
#define UFS2_SB_FRAG      0x38
#define UFS2_SB_FRAGSHIFT 0x60
#define UFS2_SB_FSBTODB   0x64
#define UFS2_SB_NINDIR    0x74
#define UFS2_SB_INOPB     0x78
#define UFS2_SB_IPG       0xB8
#define UFS2_SB_FPG       0xBC
#define UFS2_SB_MAGIC     0x55C

// UFS2 on-disk inode, 256 bytes.
typedef struct PACKED {
    u16 di_mode;
    i16 di_nlink;
    u32 di_uid;
    u32 di_gid;
    u32 di_blksize;
    u64 di_size;
    u64 di_blocks;
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
#define UFS2_IFDIR 0040000
#define UFS2_IFREG 0100000

// Directory entry header; the name (d_namlen bytes, then a NUL) follows, and
// d_reclen covers the whole record including padding.
typedef struct PACKED {
    u32 d_ino;                // 0 = unused slot
    u16 d_reclen;
    u8  d_type;
    u8  d_namlen;
} ufs2_direct_t;

// Look for a UFS2 filesystem on an ATA drive (0 master, 1 slave).
bool ufs2_mount(u8 drive);

extern const vfs_ops_t ufs2_vfs_ops;
