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
#define VN_LNK  5              // symbolic link

struct mount;

typedef struct {
    const struct mount* mnt;
    u64 id;                    // driver's identity for the object (inode, cluster, ...)
    u64 aux;                   // driver's extra locator (e.g. where its dir entry lives)
    u64 size;
    u64 mtime;                 // last modified, seconds since 1970 (0: unknown)
    u64 atime, ctime;          // last read, last changed (inode); 0: as mtime
    u32 type;                  // VN_*
    // Ownership and permissions, from filesystems that keep them. mode is
    // the permission bits only (07777); 0 means the filesystem has none,
    // and callers make something sensible up.
    u32 mode;
    u32 uid, gid;
    u32 nlink;                 // 0: unknown
} vnode_t;

// Attribute changes (setattr): which fields of vattr_t to apply.
#define VATTR_MODE  0x1
#define VATTR_UID   0x2
#define VATTR_GID   0x4
#define VATTR_ATIME 0x8
#define VATTR_MTIME 0x10
typedef struct {
    u32 mode, uid, gid;
    u64 atime, mtime;
} vattr_t;

// Space and inodes, in units of bsize bytes (statfs).
typedef struct {
    u64 bsize;
    u64 blocks, bfree, bavail;
    u64 files, ffree;
    u32 namemax;
} vfs_statfs_t;

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
    // Make a new empty file (VN_REG) or directory (VN_DIR) in dir, with
    // permission bits `mode` (umask already applied).
    i32 (*create)(const vnode_t* dir, const char* name, u32 type, u32 mode, vnode_t* out);
    i32 (*unlink)(const vnode_t* dir, const char* name);    // files and empty dirs
    i32 (*truncate)(vnode_t* node, u64 size);
    // Move olddir/oldname to newdir/newname (same filesystem), replacing a
    // file already at the destination.
    i32 (*rename)(const vnode_t* olddir, const char* oldname,
                  const vnode_t* newdir, const char* newname);

    // Optional, for filesystems that have them.
    i64 (*readlink)(vnode_t* node, char* buf, u64 len);     // bytes, no NUL
    // A "magic" link (procfs's fd/N): resolve straight to the object it
    // stands for, which may have no path (a pipe). Optional.
    i32 (*follow)(vnode_t* link, vnode_t* out);
    i32 (*symlink)(const vnode_t* dir, const char* name, const char* target, vnode_t* out);
    i32 (*link)(const vnode_t* dir, const char* name, const vnode_t* target);
    i32 (*setattr)(vnode_t* node, u32 mask, const vattr_t* a);
    // An open of node is starting (devices check they can be opened).
    i32 (*open)(vnode_t* node, u32 flags);
    // Refresh node's size, times, mode and links from the filesystem (for
    // fstat on a file opened long ago).
    i32 (*getattr)(vnode_t* node);
    i32 (*statfs)(const struct mount* m, vfs_statfs_t* out);
    // The last open file on node is gone. With keeps_unlinked set, the
    // driver lets open files be unlinked (and renamed over): it keeps the
    // object until this is called -- the Unix rule.
    void (*release)(vnode_t* node);
    // The system is going down: write everything out and mark the
    // filesystem clean.
    void (*shutdown)(const struct mount* m);
    bool keeps_unlinked;
} vfs_ops_t;

typedef struct mount {
    char point[VFS_PATH_MAX];
    const vfs_ops_t* ops;
    u32 index;                 // reported as st_dev
    void* priv;                // the driver's state for this mount
} mount_t;

bool vfs_mount(const char* point, const vfs_ops_t* ops);
// The same, for a driver that can mount several filesystems at once: priv
// is handed back as node->mnt->priv (already set when root() is called).
bool vfs_mount_with(const char* point, const vfs_ops_t* ops, void* priv);
const mount_t* vfs_mount_at(u32 i);      // the i-th mount, NULL past the end

// Normalise `path` against the current process's working directory.
i32 vfs_normalize(const char* path, char* out);

// Resolve a path to a node, following symbolic links (all of them, or all
// but a final one with _nofollow -- for lstat, readlink, unlink...).
i32 vfs_lookup(const char* path, vnode_t* out);
i32 vfs_lookup_nofollow(const char* path, vnode_t* out);
// Resolve the directory a path would live in, and its final component --
// for creating and deleting. Fails with EEXIST/ENOENT as appropriate only in
// the caller; this just splits and resolves.
i32 vfs_lookup_parent(const char* path, vnode_t* dir_out, char* name_out);

i64 vfs_read(vnode_t* node, u64 off, void* buf, u64 len);
i64 vfs_write(vnode_t* node, u64 off, const void* buf, u64 len);
i32 vfs_readdir(const vnode_t* dir, u32 index, char* name_out, vnode_t* out);
// Same, by path, with mount points that live in the directory listed first.
i32 vfs_readdir_path(const char* path, u32 index, char* name_out, vnode_t* out);
i32 vfs_create(const char* path, u32 type, vnode_t* out);     // mode 0666/0777 & ~umask
i32 vfs_create_mode(const char* path, u32 type, u32 mode, vnode_t* out);
i32 vfs_symlink(const char* target, const char* linkpath);
i32 vfs_link(const char* oldpath, const char* newpath);
i64 vfs_readlink(const char* path, char* buf, u64 len);
i32 vfs_setattr(vnode_t* node, u32 mask, const vattr_t* a);
void vfs_getattr(vnode_t* node);
void vfs_shutdown_all(void);             // before powering off         // bring an open file's node up to date
i32 vfs_statfs(const vnode_t* node, vfs_statfs_t* out);
i32 vfs_unlink(const char* path, bool want_dir);
i32 vfs_truncate(vnode_t* node, u64 size);
i32 vfs_rename(const char* oldpath, const char* newpath);

// Change the current process's working directory.
i32 vfs_chdir(const char* path);

// Convenience: read up to max bytes of the file at path. Bytes read or -errno.
i64 vfs_read_file(const char* path, void* buf, u64 max);

// Print the mount table (for the kernel shell).
void vfs_list_mounts(void);
