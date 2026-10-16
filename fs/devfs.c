// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// devfs: a tiny filesystem of device nodes, mounted at /dev.
//
//   console     the framebuffer console's terminal
//   tty         your controlling terminal, whichever it is (file_open
//               resolves it to console or pts/N)
//   ptmx        a new pseudo-terminal pair per open (file_open again)
//   pts/N       pseudo-terminal slaves
//   null, zero, serial (COM1), fd/N and std{in,out,err}
//   fb0         the framebuffer (drivers/fb.c), which programs may mmap
//   input/mice  the mouse, as 3-byte PS/2 packets (drivers/mouse.c)
//   tty0, tty1  the console again: Linux's names for "the current virtual
//               terminal" and the first one, which is all there is here

#include <types.h>
#include <devfs.h>
#include <vfs.h>
#include <tty.h>
#include <proc.h>
#include <errno.h>
#include <memset.h>
#include <serial.h>
#include <rtc.h>
#include <kprintf.h>
#include <klibc/string.h>
#include <fb.h>
#include <mouse.h>

enum { DEV_ROOT = 1, DEV_TTY, DEV_NULL, DEV_ZERO, DEV_SERIAL, DEV_FD,
       DEV_STDIN, DEV_STDOUT, DEV_STDERR, DEV_CONSOLE, DEV_PTMX, DEV_PTS,
       DEV_FB0, DEV_INPUT, DEV_TTY0, DEV_TTY1, DEV_COUNT };
#define DEV_MICE 0x3000              // input/mice

// fd/N nodes: DEV_FD_BASE + N. Opening them (or stdin/stdout/stderr) is
// handled by file_open, which hands back the descriptor's own file.
#define DEV_FD_BASE  0x1000
#define DEV_PTS_BASE 0x2000          // pts/N

static const char* const dev_names[DEV_COUNT] = {
    [DEV_TTY]  = "tty",
    [DEV_NULL] = "null",
    [DEV_ZERO] = "zero",
    [DEV_SERIAL] = "serial",   // COM1, write-only: logs and test results
    [DEV_FD] = "fd",
    [DEV_STDIN] = "stdin",
    [DEV_STDOUT] = "stdout",
    [DEV_STDERR] = "stderr",
    [DEV_CONSOLE] = "console",
    [DEV_PTMX] = "ptmx",
    [DEV_PTS] = "pts",
    [DEV_FB0] = "fb0",
    [DEV_INPUT] = "input",
    [DEV_TTY0] = "tty0",
    [DEV_TTY1] = "tty1",
};

static void make_node(u64 id, vnode_t* out) {
    out->id = id;
    out->aux = 0;
    out->size = id == DEV_FB0 ? fb_info()->size : 0;
    out->mtime = rtc_now();                // devices are always "now"
    out->type = (id == DEV_ROOT || id == DEV_FD || id == DEV_PTS || id == DEV_INPUT) ? VN_DIR : VN_CHR;
}

static i32 devfs_root(vnode_t* out) {
    make_node(DEV_ROOT, out);
    return 0;
}

static bool number(const char* s, u64* out) {
    u64 n = 0;
    if (!*s) return false;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || n > 100000) return false;
        n = n * 10 + (u64)(*s - '0');
    }
    *out = n;
    return true;
}

static i32 devfs_lookup(const vnode_t* dir, const char* name, vnode_t* out) {
    u64 n;
    if (dir->id == DEV_FD) {
        if (!number(name, &n)) return -ENOENT;
        make_node(DEV_FD_BASE + n, out);
        return 0;
    }
    if (dir->id == DEV_PTS) {
        if (!number(name, &n) || !pty_exists((u32)n)) return -ENOENT;
        make_node(DEV_PTS_BASE + n, out);
        return 0;
    }
    if (dir->id == DEV_INPUT) {
        if (strcmp(name, "mice")) return -ENOENT;
        make_node(DEV_MICE, out);
        return 0;
    }
    if (dir->id != DEV_ROOT) return -ENOTDIR;
    for (u64 id = DEV_TTY; id < DEV_COUNT; id++) {
        if (!strcmp(name, dev_names[id])) {
            make_node(id, out);
            return 0;
        }
    }
    return -ENOENT;
}

static i32 devfs_readdir(const vnode_t* dir, u32 index, char* name_out, vnode_t* out) {
    if (dir->id == DEV_FD) return 0;                 // its entries are per-process
    if (dir->id == DEV_PTS) {
        for (u32 n = 0; n < PTY_MAX; n++) {
            if (!pty_exists(n) || index--) continue;
            ksnprintf(name_out, VFS_NAME_MAX, "%u", n);
            make_node(DEV_PTS_BASE + n, out);
            return 1;
        }
        return 0;
    }
    if (dir->id == DEV_INPUT) {
        if (index) return 0;
        strcpy(name_out, "mice");
        make_node(DEV_MICE, out);
        return 1;
    }
    if (dir->id != DEV_ROOT) return -ENOTDIR;
    u64 id = DEV_TTY + index;
    if (id >= DEV_COUNT) return 0;
    strcpy(name_out, dev_names[id]);
    make_node(id, out);
    return 1;
}

// The terminal a node stands for. /dev/tty is the caller's controlling
// terminal (or the console, for the kernel and its first children).
tty_t* devfs_tty(const vnode_t* node) {
    if (!node->mnt || node->mnt->ops != &devfs_vfs_ops) return NULL;
    if (node->id == DEV_CONSOLE || node->id == DEV_TTY0 || node->id == DEV_TTY1) return &tty_console;
    if (node->id == DEV_TTY) {
        tty_t* c = proc_current()->ctty;
        return c ? c : &tty_console;
    }
    if (node->id >= DEV_PTS_BASE && node->id < DEV_PTS_BASE + PTY_MAX) return pty_slave((u32)(node->id - DEV_PTS_BASE));
    return NULL;
}

bool devfs_is_tty(const vnode_t* node) {
    return devfs_tty(node) != NULL;
}

bool devfs_is_ptmx(const vnode_t* node) {
    return node->mnt && node->mnt->ops == &devfs_vfs_ops && node->id == DEV_PTMX;
}

static bool is_devfs(const vnode_t* node) {
    return node->mnt && node->mnt->ops == &devfs_vfs_ops;
}

bool devfs_is_fb(const vnode_t* node) {
    return is_devfs(node) && node->id == DEV_FB0;
}

bool devfs_is_mouse(const vnode_t* node) {
    return is_devfs(node) && node->id == DEV_MICE;
}

i32 devfs_pts_number(const vnode_t* node) {
    if (!node->mnt || node->mnt->ops != &devfs_vfs_ops) return -1;
    if (node->id >= DEV_PTS_BASE && node->id < DEV_PTS_BASE + PTY_MAX) return (i32)(node->id - DEV_PTS_BASE);
    return -1;
}

static i64 devfs_read(vnode_t* node, u64 off, void* buf, u64 len) {
    (void)off;
    tty_t* t = devfs_tty(node);
    if (t) return tty_read(t, buf, len, false);
    switch (node->id) {
        case DEV_NULL:
        case DEV_SERIAL: return 0;
        case DEV_ZERO:
            memset(buf, 0, (u32)(len > 0xFFFFFFFFULL ? 0xFFFFFFFFULL : len));
            return (i64)len;
        case DEV_FB0:  return fb_read(off, buf, len);
        case DEV_MICE: return mouse_read(buf, len, false);
        default:       return -ENXIO;
    }
}

static i64 devfs_write(vnode_t* node, u64 off, const void* buf, u64 len) {
    (void)off;
    tty_t* t = devfs_tty(node);
    if (t) return tty_write(t, buf, len, false);
    switch (node->id) {
        case DEV_NULL:
        case DEV_ZERO: return (i64)len;
        case DEV_FB0:  return fb_write(off, buf, len);
        case DEV_MICE: return mouse_write(buf, len);
        case DEV_SERIAL: {
            const char* p = (const char*)buf;
            for (u64 i = 0; i < len; i++) kserialputchar(p[i]);
            return (i64)len;
        }
        default:       return -ENXIO;
    }
}

// A pts is opened: it must exist and be unlocked. (The last close is
// release(), below.)
static i32 devfs_open(vnode_t* node, u32 flags) {
    (void)flags;
    i32 n = devfs_pts_number(node);
    if (n < 0) return 0;
    if (!pty_slave((u32)n)) return -EIO;
    pty_slave_opened((u32)n);
    return 0;
}

// mmap of the framebuffer: the physical memory behind [off, off + len).
static i64 devfs_mmap(vnode_t* node, u64 off, u64 len) {
    if (node->id != DEV_FB0) return -ENODEV;
    const fb_info_t* fb = fb_info();
    if (off > fb->size || len > fb->size - off) return -EINVAL;
    return (i64)(fb->phys + off);
}

static void devfs_release(vnode_t* node) {
    i32 n = devfs_pts_number(node);
    if (n >= 0) pty_slave_closed((u32)n);
}

const vfs_ops_t devfs_vfs_ops = {
    .type = "devfs",
    .root = devfs_root,
    .lookup = devfs_lookup,
    .read = devfs_read,
    .write = devfs_write,
    .readdir = devfs_readdir,
    .open = devfs_open,
    .release = devfs_release,
    .mmap = devfs_mmap,
};
