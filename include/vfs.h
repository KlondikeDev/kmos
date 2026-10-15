// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>

// The virtual filesystem: one directory tree stitched together from several
// filesystems, each mounted at a path ("/" for the boot disk, "/ufs", "/dev").
//
// Paths are resolved by the VFS: relative ones against the calling process's
// working directory, "." and ".." folded away, then the longest mount point
// picked, then the remainder walked one component at a time through that
// filesystem's lookup(). Drivers only ever see nodes and single names.
//
// A vnode is a small value describing one file or directory. It is copied
// freely (open files keep their own copy), so drivers must put everything
// they need to find the object again into id/aux.

#define VFS_PATH_MAX   256
#define VFS_NAME_MAX   64      // longest name, with NUL
#define VFS_MAX_MOUNTS 8

#define VN_REG  1              // regular file
#define VN_DIR  2
#define VN_CHR  3              // character device
#define VN_FIFO 4              // pipe

struct mount;

typedef struct {
    const struct mount* mnt;
    u64 id;                    // driver's identity for the object (inode, cluster, ...)
    u64 aux;                   // driver's extra locator (e.g. where its dir entry lives)
    u64 size;
    u32 type;                  // VN_*
} vnode_t;

// Every operation returns a value >= 0 on success or -errno. A driver leaves
// an operation NULL if it does not support it; the VFS then reports EROFS
// (for writes) or ENOSYS.
typedef struct {
    const char* type;          // "fat32", "ufs2", "devfs", ...
    i32 (*root)(vnode_t* out);
    // Find `name` (one component, never "." or "..") in directory `dir`.
    i32 (*lookup)(const vnode_t* dir, const char* name, vnode_t* out);
    i64 (*read)(vnode_t* node, u64 off, void* buf, u64 len);
    i64 (*write)(vnode_t* node, u64 off, const void* buf, u64 len);
    // index-th entry of a directory, "." and ".." excluded: 1 found, 0 end.
    i32 (*readdir)(const vnode_t* dir, u32 index, char* name_out, vnode_t* out);
    // Make a new empty file (VN_REG) or directory (VN_DIR) in dir.
    i32 (*create)(const vnode_t* dir, const char* name, u32 type, vnode_t* out);
    i32 (*unlink)(const vnode_t* dir, const char* name);    // files and empty dirs
    i32 (*truncate)(vnode_t* node, u64 size);
    // Move olddir/oldname to newdir/newname (same filesystem), replacing a
    // file already at the destination.
    i32 (*rename)(const vnode_t* olddir, const char* oldname,
                  const vnode_t* newdir, const char* newname);
} vfs_ops_t;

typedef struct mount {
    char point[VFS_PATH_MAX];
    const vfs_ops_t* ops;
    u32 index;                 // reported as st_dev
} mount_t;

bool vfs_mount(const char* point, const vfs_ops_t* ops);

// Normalise `path` against the current process's working directory.
i32 vfs_normalize(const char* path, char* out);

// Resolve a path to a node.
i32 vfs_lookup(const char* path, vnode_t* out);
// Resolve the directory a path would live in, and its final component --
// for creating and deleting. Fails with EEXIST/ENOENT as appropriate only in
// the caller; this just splits and resolves.
i32 vfs_lookup_parent(const char* path, vnode_t* dir_out, char* name_out);

i64 vfs_read(vnode_t* node, u64 off, void* buf, u64 len);
i64 vfs_write(vnode_t* node, u64 off, const void* buf, u64 len);
i32 vfs_readdir(const vnode_t* dir, u32 index, char* name_out, vnode_t* out);
// Same, by path, with mount points that live in the directory listed first.
i32 vfs_readdir_path(const char* path, u32 index, char* name_out, vnode_t* out);
i32 vfs_create(const char* path, u32 type, vnode_t* out);
i32 vfs_unlink(const char* path, bool want_dir);
i32 vfs_truncate(vnode_t* node, u64 size);
i32 vfs_rename(const char* oldpath, const char* newpath);

// Change the current process's working directory.
i32 vfs_chdir(const char* path);

// Convenience: read up to max bytes of the file at path. Bytes read or -errno.
i64 vfs_read_file(const char* path, void* buf, u64 max);

// Print the mount table (for the kernel shell).
void vfs_list_mounts(void);
