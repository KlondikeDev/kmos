// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>

// The virtual filesystem: one directory tree stitched together from several
// filesystems. Each filesystem is mounted at a path ("/" for the boot disk,
// "/ufs" for the second one, ...). The VFS turns any path -- relative to the
// working directory or absolute, with "." and ".." -- into a clean absolute
// path, picks the mount it falls under, and hands the driver the rest of
// the path, always absolute and always normalised ("/", "/a", "/a/b").

#define VFS_PATH_MAX  256
#define VFS_NAME_MAX  64      // longest name reported by readdir, with NUL
#define VFS_MAX_MOUNTS 8

typedef struct {
    const char* type;     // "fat32", "ufs2", ...
    // Read up to max bytes of a file. Returns bytes read, or -1.
    i32 (*read_file)(const char* path, void* buf, u32 max);
    // index-th entry of a directory, "." and ".." excluded. name_out holds
    // VFS_NAME_MAX bytes. Returns 1 found, 0 past the end, -1 not a dir.
    i32 (*readdir)(const char* path, u32 index, char* name_out, u32* size_out, bool* is_dir_out);
} vfs_ops_t;

bool vfs_mount(const char* point, const vfs_ops_t* ops);

// Same contracts as the vfs_ops_t members, but for any path in the tree.
i32 vfs_read_file(const char* path, void* buf, u32 max);
i32 vfs_readdir(const char* path, u32 index, char* name_out, u32* size_out, bool* is_dir_out);

bool vfs_chdir(const char* path);
const char* vfs_pwd(void);

// Print the mount table (for the kernel shell).
void vfs_list_mounts(void);
