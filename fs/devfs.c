// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// devfs: a tiny filesystem of device nodes, mounted at /dev.

#include <types.h>
#include <devfs.h>
#include <vfs.h>
#include <tty.h>
#include <errno.h>
#include <memset.h>
#include <serial.h>

enum { DEV_ROOT = 1, DEV_TTY, DEV_NULL, DEV_ZERO, DEV_SERIAL, DEV_COUNT };

static const char* const dev_names[DEV_COUNT] = {
    [DEV_TTY]  = "tty",
    [DEV_NULL] = "null",
    [DEV_ZERO] = "zero",
    [DEV_SERIAL] = "serial",   // COM1, write-only: logs and test results
};

static void make_node(u64 id, vnode_t* out) {
    out->id = id;
    out->aux = 0;
    out->size = 0;
    out->type = id == DEV_ROOT ? VN_DIR : VN_CHR;
}

static i32 devfs_root(vnode_t* out) {
    make_node(DEV_ROOT, out);
    return 0;
}

static bool name_eq(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static i32 devfs_lookup(const vnode_t* dir, const char* name, vnode_t* out) {
    if (dir->id != DEV_ROOT) return -ENOTDIR;
    for (u64 id = DEV_TTY; id < DEV_COUNT; id++) {
        if (name_eq(name, dev_names[id])) {
            make_node(id, out);
            return 0;
        }
    }
    return -ENOENT;
}

static i32 devfs_readdir(const vnode_t* dir, u32 index, char* name_out, vnode_t* out) {
    if (dir->id != DEV_ROOT) return -ENOTDIR;
    u64 id = DEV_TTY + index;
    if (id >= DEV_COUNT) return 0;
    const char* n = dev_names[id];
    u32 i = 0;
    for (; n[i]; i++) name_out[i] = n[i];
    name_out[i] = '\0';
    make_node(id, out);
    return 1;
}

static i64 devfs_read(vnode_t* node, u64 off, void* buf, u64 len) {
    (void)off;
    switch (node->id) {
        case DEV_TTY:  return tty_read(buf, len, false);
        case DEV_NULL:
        case DEV_SERIAL: return 0;
        case DEV_ZERO:
            memset(buf, 0, (u32)(len > 0xFFFFFFFFULL ? 0xFFFFFFFFULL : len));
            return (i64)len;
        default:       return -ENXIO;
    }
}

static i64 devfs_write(vnode_t* node, u64 off, const void* buf, u64 len) {
    (void)off;
    switch (node->id) {
        case DEV_TTY:  return tty_write(buf, len);
        case DEV_NULL:
        case DEV_ZERO: return (i64)len;
        case DEV_SERIAL: {
            const char* p = (const char*)buf;
            for (u64 i = 0; i < len; i++) kserialputchar(p[i]);
            return (i64)len;
        }
        default:       return -ENXIO;
    }
}

const vfs_ops_t devfs_vfs_ops = {
    .type = "devfs",
    .root = devfs_root,
    .lookup = devfs_lookup,
    .read = devfs_read,
    .write = devfs_write,
    .readdir = devfs_readdir,
};

bool devfs_is_tty(const vnode_t* node) {
    return node->mnt && node->mnt->ops == &devfs_vfs_ops && node->id == DEV_TTY;
}
