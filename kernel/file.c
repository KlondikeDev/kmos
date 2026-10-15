// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <types.h>
#include <file.h>
#include <vfs.h>
#include <proc.h>
#include <signal.h>
#include <errno.h>
#include <memory.h>
#include <memset.h>
#include <klibc/string.h>

// ---------------------------------------------------------------------------
// Open vnodes
//
// vfs_lookup returns a fresh copy of a node each time. Every open file
// needs to see the same one, so open nodes are interned here, keyed by
// mount and the driver's locator: aux when it has one (FAT32's directory
// entry position, which never changes even when the first cluster does),
// otherwise id (UFS2's inode number, devfs's device number).
// ---------------------------------------------------------------------------

typedef struct open_vnode {
    vnode_t node;                      // first member: a vnode_t* is an open_vnode*
    u32 refs;
    struct open_vnode* next;
} open_vnode_t;

static open_vnode_t* open_vnodes;

static u64 vnode_key(const vnode_t* n) {
    return n->aux ? n->aux : n->id;
}

static vnode_t* vnode_intern(const vnode_t* n) {
    for (open_vnode_t* o = open_vnodes; o; o = o->next) {
        if (o->node.mnt == n->mnt && vnode_key(&o->node) == vnode_key(n)) {
            o->refs++;
            return &o->node;
        }
    }
    open_vnode_t* o = kmalloc(sizeof(open_vnode_t));
    if (!o) return NULL;
    o->node = *n;
    o->refs = 1;
    o->next = open_vnodes;
    open_vnodes = o;
    return &o->node;
}

bool vnode_is_open(const vnode_t* n) {
    for (open_vnode_t* o = open_vnodes; o; o = o->next) {
        if (o->node.mnt == n->mnt && vnode_key(&o->node) == vnode_key(n)) return true;
    }
    return false;
}

static void vnode_release(vnode_t* n) {
    open_vnode_t* o = (open_vnode_t*)n;
    if (--o->refs) return;
    for (open_vnode_t** pp = &open_vnodes; *pp; pp = &(*pp)->next) {
        if (*pp == o) { *pp = o->next; break; }
    }
    kfree(o);
}

// ---------------------------------------------------------------------------
// Files backed by the VFS
// ---------------------------------------------------------------------------

static i64 vnode_file_read(file_t* f, void* buf, u64 len) {
    i64 n = vfs_read(f->node, f->pos, buf, len);
    if (n > 0 && f->node->type == VN_REG) f->pos += (u64)n;
    return n;
}

static i64 vnode_file_write(file_t* f, const void* buf, u64 len) {
    if (f->node->type == VN_REG && (f->flags & O_APPEND)) f->pos = f->node->size;
    i64 n = vfs_write(f->node, f->pos, buf, len);
    if (n > 0 && f->node->type == VN_REG) f->pos += (u64)n;
    return n;
}

static void vnode_file_release(file_t* f) {
    if (f->path) kfree(f->path);
    if (f->node) vnode_release(f->node);
}

static const file_ops_t vnode_file_ops = {
    .read = vnode_file_read,
    .write = vnode_file_write,
    .release = vnode_file_release,
};

static file_t* file_alloc(const file_ops_t* ops, u32 flags) {
    file_t* f = kmalloc(sizeof(file_t));
    if (!f) return NULL;
    memset(f, 0, sizeof(*f));
    f->ops = ops;
    f->refs = 1;
    f->flags = flags;
    return f;
}

i32 file_open(const char* path, u32 flags, file_t** out) {
    u32 acc = flags & O_ACCMODE;
    if (acc == 3) return -EINVAL;

    vnode_t node;
    i32 r = vfs_lookup(path, &node);
    if (r == -ENOENT && (flags & O_CREAT)) {
        r = vfs_create(path, VN_REG, &node);
    } else if (r == 0 && (flags & O_CREAT) && (flags & O_EXCL)) {
        return -EEXIST;
    }
    if (r < 0) return r;

    if (node.type == VN_DIR) {
        if (acc != O_RDONLY) return -EISDIR;
    } else if (flags & O_DIRECTORY) {
        return -ENOTDIR;
    }

    file_t* f = file_alloc(&vnode_file_ops, flags);
    if (!f) return -ENOMEM;
    f->node = vnode_intern(&node);
    if (!f->node) { kfree(f); return -ENOMEM; }

    // Truncate the shared node, so every open of this file sees it.
    if ((flags & O_TRUNC) && acc != O_RDONLY && node.type == VN_REG) {
        r = vfs_truncate(f->node, 0);
        if (r < 0) { file_unref(f); return r; }
    }
    if (node.type == VN_DIR) {
        // Directory listings include mount points, which needs the path.
        f->path = kmalloc(VFS_PATH_MAX);
        if (!f->path) { file_unref(f); return -ENOMEM; }
        vfs_normalize(path, f->path);
    }
    *out = f;
    return 0;
}

void file_ref(file_t* f) {
    f->refs++;
}

void file_unref(file_t* f) {
    if (--f->refs) return;
    if (f->ops->release) f->ops->release(f);
    kfree(f);
}

i64 file_read(file_t* f, void* buf, u64 len) {
    if ((f->flags & O_ACCMODE) == O_WRONLY) return -EBADF;
    if (!f->ops->read) return -EINVAL;
    return f->ops->read(f, buf, len);
}

i64 file_write(file_t* f, const void* buf, u64 len) {
    if ((f->flags & O_ACCMODE) == O_RDONLY) return -EBADF;
    if (!f->ops->write) return -EINVAL;
    return f->ops->write(f, buf, len);
}

// ---------------------------------------------------------------------------
// Pipes
// ---------------------------------------------------------------------------

// A ring buffer with a count of open ends. Readers block while it is empty
// and a writer still exists; writers block while it is full and a reader
// still exists. Everyone blocks on the pipe's address.
typedef struct {
    vnode_t node;                      // what fstat/lseek see: a FIFO
    u8  buf[PIPE_BUF_SIZE];
    u32 head;                          // next byte to read
    u32 count;                         // bytes buffered
    u32 readers;
    u32 writers;
} pipe_t;

static i64 pipe_read(file_t* f, void* dst, u64 len) {
    pipe_t* p = f->priv;
    if (len == 0) return 0;
    while (p->count == 0) {
        if (p->writers == 0) return 0;                       // EOF
        if (f->flags & O_NONBLOCK) return -EAGAIN;
        sched_block_on(p);
        if (signal_pending(proc_current())) return -EINTR;
    }
    u64 n = len < p->count ? len : p->count;
    u8* out = (u8*)dst;
    for (u64 i = 0; i < n; i++) {
        out[i] = p->buf[p->head];
        p->head = (p->head + 1) % PIPE_BUF_SIZE;
    }
    p->count -= (u32)n;
    sched_wake_obj(p);                                       // room for writers
    return (i64)n;
}

static i64 pipe_write(file_t* f, const void* src, u64 len) {
    pipe_t* p = f->priv;
    const u8* in = (const u8*)src;
    u64 done = 0;
    while (done < len) {
        if (p->readers == 0) {
            signal_send(proc_current(), SIGPIPE);
            return done ? (i64)done : -EPIPE;
        }
        if (p->count == PIPE_BUF_SIZE) {
            if (f->flags & O_NONBLOCK) return done ? (i64)done : -EAGAIN;
            sched_wake_obj(p);                               // let readers drain
            sched_block_on(p);
            if (signal_pending(proc_current())) return done ? (i64)done : -EINTR;
            continue;
        }
        u32 tail = (p->head + p->count) % PIPE_BUF_SIZE;
        p->buf[tail] = in[done++];
        p->count++;
    }
    sched_wake_obj(p);
    return (i64)done;
}

static void pipe_release(file_t* f) {
    pipe_t* p = f->priv;
    if ((f->flags & O_ACCMODE) == O_RDONLY) p->readers--;
    else p->writers--;
    sched_wake_obj(p);                 // EOF for readers, EPIPE for writers
    if (p->readers == 0 && p->writers == 0) kfree(p);
}

static const file_ops_t pipe_ops = {
    .read = pipe_read,
    .write = pipe_write,
    .release = pipe_release,
};

i32 pipe_create(file_t** read_end, file_t** write_end) {
    pipe_t* p = kmalloc(sizeof(pipe_t));
    if (!p) return -ENOMEM;
    memset(p, 0, sizeof(*p));

    file_t* r = file_alloc(&pipe_ops, O_RDONLY);
    file_t* w = file_alloc(&pipe_ops, O_WRONLY);
    if (!r || !w) {
        if (r) kfree(r);
        if (w) kfree(w);
        kfree(p);
        return -ENOMEM;
    }
    r->priv = w->priv = p;
    p->node.type = VN_FIFO;
    r->node = w->node = &p->node;
    p->readers = p->writers = 1;
    *read_end = r;
    *write_end = w;
    return 0;
}

// ---------------------------------------------------------------------------
// Descriptor tables
// ---------------------------------------------------------------------------

i32 fd_install(process_t* p, file_t* f, i32 lowest) {
    for (i32 fd = lowest < 0 ? 0 : lowest; fd < MAX_FDS; fd++) {
        if (!p->fds[fd]) {
            p->fds[fd] = f;
            p->fd_cloexec &= ~(1u << fd);
            return fd;
        }
    }
    return -EMFILE;
}

file_t* fd_get(process_t* p, i32 fd) {
    if (fd < 0 || fd >= MAX_FDS) return NULL;
    return p->fds[fd];
}

i32 fd_close(process_t* p, i32 fd) {
    file_t* f = fd_get(p, fd);
    if (!f) return -EBADF;
    p->fds[fd] = NULL;
    p->fd_cloexec &= ~(1u << fd);
    file_unref(f);
    return 0;
}

void fd_close_all(process_t* p) {
    for (i32 fd = 0; fd < MAX_FDS; fd++) {
        if (p->fds[fd]) fd_close(p, fd);
    }
}

void fd_close_on_exec(process_t* p) {
    for (i32 fd = 0; fd < MAX_FDS; fd++) {
        if (p->fds[fd] && (p->fd_cloexec & (1u << fd))) fd_close(p, fd);
    }
}

void fd_inherit(process_t* child, const process_t* parent) {
    for (i32 fd = 0; fd < MAX_FDS; fd++) {
        child->fds[fd] = parent->fds[fd];
        if (child->fds[fd]) file_ref(child->fds[fd]);
    }
    child->fd_cloexec = parent->fd_cloexec;
}
