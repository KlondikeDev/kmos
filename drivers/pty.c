// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// Pseudo-terminals, the Unix 98 way Linux has them.
//
// Opening /dev/ptmx makes a pair and returns the master. The slave is a
// full terminal (tty_t: line discipline, termios, job control) that
// appears as /dev/pts/N once unlocked (TIOCSPTLCK). Whatever programs write
// to the slave lands in a buffer the master reads; whatever is written to
// the master is typed into the slave. Terminal emulators (xterm), script,
// ssh and tmux are built on this.
//
// When the master closes, the slave hangs up (SIGHUP to its session). The
// pair is freed once both sides are closed.

#include <types.h>
#include <tty.h>
#include <file.h>
#include <proc.h>
#include <signal.h>
#include <memory.h>
#include <memset.h>
#include <memcpy.h>
#include <errno.h>

#define PTY_BUF 8192

typedef struct {
    bool used;
    bool master_open;
    bool locked;                     // until TIOCSPTLCK(0): /dev/pts/N refuses to open
    u32 slave_opens;                 // 0 or 1: devfs tells us first open / last close
    bool slave_was_opened;
    tty_t slave;
    u8 out[PTY_BUF];                 // slave output, waiting for the master
    u32 out_head, out_count;
} pty_t;

static pty_t ptys[PTY_MAX];

static bool slave_output(tty_t* t, char c) {
    pty_t* p = t->priv;
    if (p->out_count == PTY_BUF) return false;
    p->out[(p->out_head + p->out_count) % PTY_BUF] = (u8)c;
    p->out_count++;
    return true;
}

static void slave_flush(tty_t* t) {
    sched_wake_obj(t->priv);        // the master may read
}

// ---- the master's file ----

static i64 master_read(file_t* f, void* dst, u64 len) {
    pty_t* p = f->priv;
    if (len == 0) return 0;
    while (p->out_count == 0) {
        // Every slave has gone away after one was opened: nothing more can come.
        if (p->slave_was_opened && p->slave_opens == 0) return -EIO;
        if (f->flags & O_NONBLOCK) return -EAGAIN;
        sched_block_on(p);
        if (signal_pending(proc_current())) return -EINTR;
    }
    u8* out = dst;
    u64 n = 0;
    while (n < len && p->out_count) {
        out[n++] = p->out[p->out_head];
        p->out_head = (p->out_head + 1) % PTY_BUF;
        p->out_count--;
    }
    sched_wake_obj(&p->slave);       // writers waiting for room
    return (i64)n;
}

static i64 master_write(file_t* f, const void* src, u64 len) {
    pty_t* p = f->priv;
    const char* in = src;
    for (u64 i = 0; i < len; i++) tty_input(&p->slave, in[i]);
    return (i64)len;
}

static u32 master_poll(file_t* f) {
    pty_t* p = f->priv;
    u32 ev = POLLOUT;
    if (p->out_count) ev |= POLLIN;
    if (p->slave_was_opened && p->slave_opens == 0) ev |= POLLHUP;
    return ev;
}

static void pty_maybe_free(pty_t* p) {
    if (!p->master_open && p->slave_opens == 0) p->used = false;
}

static void master_release(file_t* f) {
    pty_t* p = f->priv;
    if (f->path) kfree(f->path);
    p->master_open = false;
    tty_hangup(&p->slave);
    pty_maybe_free(p);
}

static const file_ops_t master_ops = {
    .read = master_read,
    .write = master_write,
    .poll = master_poll,
    .release = master_release,
};

i32 pty_open_master(file_t** out) {
    pty_t* p = NULL;
    for (u32 i = 0; i < PTY_MAX && !p; i++) if (!ptys[i].used) p = &ptys[i];
    if (!p) return -ENOSPC;                      // Linux says ENOSPC too
    file_t* f = kmalloc(sizeof(file_t));
    if (!f) return -ENOMEM;
    memset(f, 0, sizeof(*f));
    memset(p, 0, sizeof(*p));
    p->used = true;
    p->master_open = true;
    p->locked = true;
    tty_setup(&p->slave, slave_output, slave_flush, p);
    f->ops = &master_ops;
    f->refs = 1;
    f->flags = O_RDWR;
    f->priv = p;
    f->path = kmalloc(16);
    if (f->path) memcpy(f->path, "/dev/ptmx", 10);
    *out = f;
    return 0;
}

bool file_is_pty_master(file_t* f) {
    return f && f->ops == &master_ops;
}

i32 pty_master_number(file_t* f) {
    return (i32)((pty_t*)f->priv - ptys);
}

void pty_master_lock(file_t* f, bool locked) {
    ((pty_t*)f->priv)->locked = locked;
}

bool pty_exists(u32 n) {
    return n < PTY_MAX && ptys[n].used;
}

tty_t* pty_slave(u32 n) {
    if (n >= PTY_MAX || !ptys[n].used || ptys[n].locked || !ptys[n].master_open) return NULL;
    return &ptys[n].slave;
}

u32 pty_index(const tty_t* t) {
    for (u32 i = 0; i < PTY_MAX; i++) if (&ptys[i].slave == t) return i;
    return ~0u;
}

void pty_slave_opened(u32 n) {
    if (n >= PTY_MAX) return;
    ptys[n].slave_opens = 1;
    ptys[n].slave_was_opened = true;
}

void pty_slave_closed(u32 n) {
    if (n >= PTY_MAX) return;
    ptys[n].slave_opens = 0;
    sched_wake_obj(&ptys[n]);        // the master reads EIO now
    pty_maybe_free(&ptys[n]);
}

tty_t* devfs_tty(const vnode_t* node);

tty_t* file_tty(file_t* f) {
    if (!f) return NULL;
    if (f->ops == &master_ops) return &((pty_t*)f->priv)->slave;
    return f->node ? devfs_tty(f->node) : NULL;
}
