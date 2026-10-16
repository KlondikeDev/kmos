// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// Local (Unix domain) sockets. See include/socket.h.
//
// Every socket is owned by exactly one thing: its open file, or -- for a
// connection nobody has accepted yet -- the listening socket's backlog.
// Peers point at each other; whichever goes first clears both pointers and
// leaves the survivor "peer gone" (it reads what is left, then EOF; writes
// get EPIPE). A datagram socket connected to a named socket keeps only the
// name and looks it up on every send, so nothing dangles.
//
// Everything runs with interrupts off on one CPU, so a syscall is atomic
// with respect to every other: no locks.

#include <types.h>
#include <socket.h>
#include <file.h>
#include <vfs.h>
#include <proc.h>
#include <signal.h>
#include <memory.h>
#include <memset.h>
#include <memcpy.h>
#include <errno.h>
#include <klibc/string.h>

#define SK_BUF       (64 * 1024)     // a stream socket's receive buffer
#define SK_BACKLOG   32
#define SK_DGRAM_MAX (64 * 1024)     // biggest message
#define SK_QUEUE_MAX (256 * 1024)    // bytes of messages waiting, per socket

#define EADDRINUSE   98
#define ECONNREFUSED 111
#define EISCONN      106
#define ENOTCONN     107
#define EPROTOTYPE   91
#define EOPNOTSUPP   95
#define EMSGSIZE     90
#define EDESTADDRREQ 89
#define ECONNRESET   104

enum { ST_NEW, ST_LISTEN, ST_CONNECTED };

typedef struct msg {
    struct msg* next;
    u64 len;
    sk_addr_t from;
    file_t* files[SK_MAX_FDS];
    u32 nfiles;
    u8 data[];
} msg_t;

// Files passed along a stream: delivered with the read that reaches `at`.
typedef struct batch {
    struct batch* next;
    u64 at;
    file_t* files[SK_MAX_FDS];
    u32 nfiles;
} batch_t;

typedef struct sock {
    vnode_t node;                    // what fstat sees (first member)
    u32 type;
    u32 state;
    sk_addr_t name;
    const mount_t* bound_mnt;        // a path name: the socket node it made
    u64 bound_id;
    sk_cred_t cred;                  // its creator: what a peer is told
    sk_cred_t peer_cred;
    struct sock* peer;               // the other end (streams, seqpacket, dgram pairs)
    sk_addr_t dest;                  // dgram connect() to a name
    bool peer_gone;                  // the other end closed, or shut its writing
    bool shut_rd, shut_wr;

    struct sock* backlog[SK_BACKLOG];   // listening: connections not yet accepted
    u32 nbacklog, max_backlog;

    u8* rbuf;                        // streams: bytes waiting to be read
    u32 rhead, rcount;
    u64 rin, rout;                   // bytes ever put in, ever taken out
    batch_t* batches;

    msg_t* mhead;                    // datagram / seqpacket: messages waiting
    msg_t* mtail;
    u64 mbytes;

    struct sock* next_all;
} sock_t;

static sock_t* all_socks;

static const file_ops_t sock_ops;

static sock_t* S(const file_t* f) {
    return (sock_t*)f->priv;
}

bool file_is_socket(const file_t* f) {
    return f && f->ops == &sock_ops;
}

u32 sock_type(const file_t* f) {
    return S(f)->type;
}

static void my_cred(sk_cred_t* c) {
    process_t* p = proc_current();
    c->pid = p->pid;
    c->uid = p->euid;
    c->gid = p->egid;
}

static sock_t* sock_new(u32 type) {
    sock_t* s = kmalloc(sizeof(sock_t));
    if (!s) return NULL;
    memset(s, 0, sizeof(*s));
    s->type = type;
    s->node.type = VN_SOCK;
    s->node.id = (u64)s;
    s->node.mode = 0777;
    my_cred(&s->cred);
    s->node.uid = s->cred.uid;
    s->node.gid = s->cred.gid;
    s->next_all = all_socks;
    all_socks = s;
    return s;
}

static void wake(sock_t* s) {
    sched_wake_obj(s);
}

static void drop_files(file_t** files, u32 n) {
    for (u32 i = 0; i < n; i++) if (files[i]) file_unref(files[i]);
}

// The end of a socket: its peer learns, its queues go.
static void sock_destroy(sock_t* s) {
    for (sock_t** pp = &all_socks; *pp; pp = &(*pp)->next_all) {
        if (*pp == s) { *pp = s->next_all; break; }
    }
    if (s->peer) {
        s->peer->peer = NULL;
        s->peer->peer_gone = true;
        wake(s->peer);
    }
    for (u32 i = 0; i < s->nbacklog; i++) sock_destroy(s->backlog[i]);
    while (s->batches) {
        batch_t* b = s->batches;
        s->batches = b->next;
        drop_files(b->files, b->nfiles);
        kfree(b);
    }
    while (s->mhead) {
        msg_t* m = s->mhead;
        s->mhead = m->next;
        drop_files(m->files, m->nfiles);
        kfree(m);
    }
    if (s->rbuf) kfree(s->rbuf);
    wake(s);
    kfree(s);
}

// ---- names ----

static bool same_name(const sk_addr_t* a, const sk_addr_t* b) {
    if (a->abstract != b->abstract || a->len != b->len) return false;
    for (u32 i = 0; i < a->len; i++) if (a->name[i] != b->name[i]) return false;
    return true;
}

// The socket a name stands for, or NULL. Paths go through the filesystem:
// the node there identifies the socket that made it.
static sock_t* lookup(const sk_addr_t* a, i32* err) {
    *err = -ECONNREFUSED;
    if (a->len == 0) { *err = -EINVAL; return NULL; }
    if (a->abstract) {
        for (sock_t* s = all_socks; s; s = s->next_all) {
            if (s->name.abstract && same_name(&s->name, a)) return s;
        }
        return NULL;
    }
    char path[VFS_PATH_MAX];
    if (a->len >= sizeof(path)) { *err = -ENAMETOOLONG; return NULL; }
    memcpy(path, a->name, a->len);
    path[a->len] = '\0';
    vnode_t n;
    i32 r = vfs_lookup(path, &n);
    if (r < 0) { *err = r; return NULL; }
    if (n.type != VN_SOCK) return NULL;
    for (sock_t* s = all_socks; s; s = s->next_all) {
        if (s->bound_mnt == n.mnt && s->bound_id == n.id) return s;
    }
    return NULL;                         // a leftover socket file: nobody listening
}

i32 sock_bind(file_t* f, const sk_addr_t* a) {
    sock_t* s = S(f);
    if (s->name.len) return -EINVAL;
    if (a->len == 0) return -EINVAL;
    if (a->abstract) {
        for (sock_t* o = all_socks; o; o = o->next_all) {
            if (o->name.abstract && same_name(&o->name, a)) return -EADDRINUSE;
        }
    } else {
        char path[VFS_PATH_MAX];
        if (a->len >= sizeof(path)) return -ENAMETOOLONG;
        memcpy(path, a->name, a->len);
        path[a->len] = '\0';
        vnode_t n;
        i32 r = vfs_create_mode(path, VN_SOCK, 0777, &n);
        if (r == -EEXIST) return -EADDRINUSE;
        if (r < 0) return r;
        // An old binding to a node with this identity is stale (its file
        // was removed and the slot reused): it can no longer be reached.
        for (sock_t* o = all_socks; o; o = o->next_all) {
            if (o->bound_mnt == n.mnt && o->bound_id == n.id) o->bound_mnt = NULL;
        }
        s->bound_mnt = n.mnt;
        s->bound_id = n.id;
    }
    s->name = *a;
    return 0;
}

// Linux's autobind: an unbound listener gets an abstract name of its own.
static void autobind(sock_t* s) {
    static u32 next = 1;
    s->name.abstract = true;
    s->name.len = 5;
    u32 v = next++;
    for (i32 i = 4; i >= 0; i--) { s->name.name[i] = "0123456789abcdef"[v & 15]; v >>= 4; }
}

i32 sock_listen(file_t* f, i32 backlog) {
    sock_t* s = S(f);
    if (s->type == SK_DGRAM) return -EOPNOTSUPP;
    if (s->state == ST_CONNECTED) return -EINVAL;
    if (!s->name.len) autobind(s);
    s->state = ST_LISTEN;
    s->max_backlog = backlog <= 0 ? 1 : backlog > SK_BACKLOG ? SK_BACKLOG : (u32)backlog;
    return 0;
}

bool sock_listening(file_t* f) {
    return S(f)->state == ST_LISTEN;
}

// ---- making connections ----

static bool give_buffer(sock_t* s) {
    if (s->type != SK_STREAM || s->rbuf) return true;
    s->rbuf = kmalloc(SK_BUF);
    return s->rbuf != NULL;
}

static void link_pair(sock_t* a, sock_t* b) {
    a->peer = b;
    b->peer = a;
    a->state = b->state = ST_CONNECTED;
    a->peer_cred = b->cred;
    b->peer_cred = a->cred;
}

i32 sock_connect(file_t* f, const sk_addr_t* a) {
    sock_t* s = S(f);
    i32 err;
    if (s->type == SK_DGRAM) {
        // Just a default destination; checked now, looked up at each send.
        sock_t* t = lookup(a, &err);
        if (!t) return err;
        if (t->type != SK_DGRAM) return -EPROTOTYPE;
        s->dest = *a;
        return 0;
    }
    if (s->state == ST_CONNECTED) return -EISCONN;
    if (s->state == ST_LISTEN) return -EINVAL;
    for (;;) {
        sock_t* l = lookup(a, &err);
        if (!l) return err;
        if (l->type != s->type) return -EPROTOTYPE;
        if (l->state != ST_LISTEN) return -ECONNREFUSED;
        if (l->nbacklog < l->max_backlog) {
            sock_t* srv = sock_new(s->type);
            if (!srv || !give_buffer(srv) || !give_buffer(s)) {
                if (srv) sock_destroy(srv);
                return -ENOMEM;
            }
            srv->name = l->name;             // getsockname on the accepted end
            srv->cred = l->cred;
            link_pair(s, srv);
            l->backlog[l->nbacklog++] = srv;
            wake(l);
            return 0;
        }
        // A full queue: wait for an accept() (as Linux does, unless asked not to).
        if (f->flags & O_NONBLOCK) return -EAGAIN;
        sched_block_on(l);
        if (signal_pending(proc_current())) return -EINTR;
    }
}

static file_t* sock_file(sock_t* s, u32 flags) {
    file_t* f = kmalloc(sizeof(file_t));
    if (!f) return NULL;
    memset(f, 0, sizeof(*f));
    f->ops = &sock_ops;
    f->refs = 1;
    f->flags = O_RDWR | (flags & O_NONBLOCK);
    f->priv = s;
    f->node = &s->node;
    return f;
}

i32 sock_accept(file_t* f, u32 flags, file_t** out, sk_addr_t* peer) {
    sock_t* l = S(f);
    if (l->state != ST_LISTEN) return -EINVAL;
    while (l->nbacklog == 0) {
        if (f->flags & O_NONBLOCK) return -EAGAIN;
        sched_block_on(l);
        if (signal_pending(proc_current())) return -EINTR;
    }
    sock_t* srv = l->backlog[0];
    file_t* nf = sock_file(srv, flags);
    if (!nf) return -ENOMEM;
    for (u32 i = 1; i < l->nbacklog; i++) l->backlog[i - 1] = l->backlog[i];
    l->nbacklog--;
    wake(l);                                  // a waiting connect() has room now
    if (peer) {
        if (srv->peer) *peer = srv->peer->name;
        else memset(peer, 0, sizeof(*peer));
    }
    *out = nf;
    return 0;
}

i32 sock_create(u32 type, u32 flags, file_t** out) {
    if (type != SK_STREAM && type != SK_DGRAM && type != SK_SEQPACKET) return -EINVAL;
    sock_t* s = sock_new(type);
    if (!s) return -ENOMEM;
    file_t* f = sock_file(s, flags);
    if (!f) { sock_destroy(s); return -ENOMEM; }
    *out = f;
    return 0;
}

i32 sock_pair(u32 type, u32 flags, file_t** a, file_t** b) {
    i32 r = sock_create(type, flags, a);
    if (r < 0) return r;
    r = sock_create(type, flags, b);
    if (r < 0) { file_unref(*a); return r; }
    sock_t* x = S(*a);
    sock_t* y = S(*b);
    if (!give_buffer(x) || !give_buffer(y)) { file_unref(*a); file_unref(*b); return -ENOMEM; }
    link_pair(x, y);
    return 0;
}

i32 sock_shutdown(file_t* f, i32 how) {
    sock_t* s = S(f);
    if (how < 0 || how > 2) return -EINVAL;
    if (s->state != ST_CONNECTED && s->type != SK_DGRAM) return -ENOTCONN;
    if (how == 0 || how == 2) s->shut_rd = true;
    if (how == 1 || how == 2) {
        s->shut_wr = true;
        if (s->peer) {
            s->peer->peer_gone = true;           // EOF for it, after what is queued
            wake(s->peer);
        }
    }
    wake(s);
    return 0;
}

// ---- data ----

static i32 epipe(u32 flags) {
    if (!(flags & SK_MSG_NOSIGNAL)) signal_send(proc_current(), SIGPIPE);
    return -EPIPE;
}

static i64 stream_send(file_t* f, sock_t* s, const u8* buf, u64 len, u32 flags,
                       file_t** files, u32 nfiles) {
    bool nonblock = (f->flags & O_NONBLOCK) || (flags & SK_MSG_DONTWAIT);
    if (s->state != ST_CONNECTED && !s->peer_gone) return -ENOTCONN;
    u64 done = 0;
    bool files_sent = nfiles == 0;
    while (done < len || !files_sent) {
        sock_t* t = s->peer;
        if (!t || s->shut_wr || t->shut_rd) return done ? (i64)done : epipe(flags);
        if (!files_sent) {
            batch_t* b = kmalloc(sizeof(batch_t));
            if (!b) return done ? (i64)done : -ENOMEM;
            memset(b, 0, sizeof(*b));
            b->at = t->rin;
            for (u32 i = 0; i < nfiles; i++) { b->files[i] = files[i]; file_ref(files[i]); }
            b->nfiles = nfiles;
            batch_t** pp = &t->batches;
            while (*pp) pp = &(*pp)->next;
            *pp = b;
            files_sent = true;
            if (len == 0) { wake(t); break; }
        }
        u32 room = SK_BUF - t->rcount;
        if (room == 0) {
            wake(t);
            if (nonblock) return done ? (i64)done : -EAGAIN;
            sched_block_on(t);
            if (signal_pending(proc_current())) return done ? (i64)done : -EINTR;
            continue;
        }
        u64 n = len - done < room ? len - done : room;
        for (u64 i = 0; i < n; i++) t->rbuf[(t->rhead + t->rcount + i) % SK_BUF] = buf[done + i];
        t->rcount += (u32)n;
        t->rin += n;
        done += n;
        wake(t);
    }
    return (i64)done;
}

// Hand over the passed files the read up to `upto` (bytes out) has reached.
static void take_batches(sock_t* s, u64 upto, file_t** files, u32* nfiles, bool peek) {
    u32 got = 0;
    for (batch_t** pp = &s->batches; *pp && (*pp)->at < upto; ) {
        batch_t* b = *pp;
        if (got + b->nfiles > SK_MAX_FDS) break;
        for (u32 i = 0; i < b->nfiles; i++) {
            files[got++] = b->files[i];
            if (peek) file_ref(b->files[i]);
        }
        if (peek) { pp = &b->next; continue; }
        *pp = b->next;
        kfree(b);
    }
    *nfiles = got;
}

static i64 stream_recv(file_t* f, sock_t* s, u8* buf, u64 len, u32 flags,
                       file_t** files, u32* nfiles) {
    bool nonblock = (f->flags & O_NONBLOCK) || (flags & SK_MSG_DONTWAIT);
    bool peek = flags & SK_MSG_PEEK;
    *nfiles = 0;
    u64 got = 0;
    for (;;) {
        // Files sent with no bytes (or before the first) can come on their own.
        if (s->rcount == 0 && s->batches && s->batches->at <= s->rout && files) {
            take_batches(s, s->rout + 1, files, nfiles, peek);
            if (*nfiles) return (i64)got;
        }
        if (s->rcount) {
            u64 n = len - got < s->rcount ? len - got : s->rcount;
            for (u64 i = 0; i < n; i++) buf[got + i] = s->rbuf[(s->rhead + i) % SK_BUF];
            if (files && !*nfiles) take_batches(s, s->rout + n, files, nfiles, peek);
            if (!peek) {
                s->rhead = (u32)((s->rhead + n) % SK_BUF);
                s->rcount -= (u32)n;
                s->rout += n;
                if (s->peer) wake(s->peer);            // room for its writes
            }
            got += n;
            if (got == len || !(flags & SK_MSG_WAITALL) || peek) return (i64)got;
            continue;
        }
        if (s->peer_gone || s->shut_rd || s->state != ST_CONNECTED) return (i64)got;   // EOF
        if (nonblock) return got ? (i64)got : -EAGAIN;
        sched_block_on(s);
        if (signal_pending(proc_current())) return got ? (i64)got : -EINTR;
    }
}

static i64 msg_send(file_t* f, sock_t* s, const u8* buf, u64 len, u32 flags,
                    const sk_addr_t* to, file_t** files, u32 nfiles) {
    bool nonblock = (f->flags & O_NONBLOCK) || (flags & SK_MSG_DONTWAIT);
    if (len > SK_DGRAM_MAX) return -EMSGSIZE;
    if (s->shut_wr) return epipe(flags);
    for (;;) {
        sock_t* t = NULL;
        i32 err = -ENOTCONN;
        if (s->type == SK_SEQPACKET) {
            t = s->peer;
            if (!t) return s->peer_gone ? epipe(flags) : -ENOTCONN;
        } else if (to && to->len) {
            t = lookup(to, &err);
        } else if (s->peer) {
            t = s->peer;
        } else if (s->dest.len) {
            t = lookup(&s->dest, &err);
        } else {
            return s->peer_gone ? -ECONNREFUSED : -EDESTADDRREQ;
        }
        if (!t) return err;
        if (t->type != s->type) return -EPROTOTYPE;
        if (t->shut_rd) return epipe(flags);
        if (t->mbytes + len > SK_QUEUE_MAX) {
            if (nonblock) return -EAGAIN;
            sched_block_on(t);
            if (signal_pending(proc_current())) return -EINTR;
            continue;
        }
        msg_t* m = kmalloc(sizeof(msg_t) + len + 1);
        if (!m) return -ENOMEM;
        memset(m, 0, sizeof(*m));
        m->len = len;
        memcpy(m->data, buf, len);
        m->from = s->name;
        for (u32 i = 0; i < nfiles; i++) { m->files[i] = files[i]; file_ref(files[i]); }
        m->nfiles = nfiles;
        if (t->mtail) t->mtail->next = m;
        else t->mhead = m;
        t->mtail = m;
        t->mbytes += len;
        wake(t);
        return (i64)len;
    }
}

static i64 msg_recv(file_t* f, sock_t* s, u8* buf, u64 len, u32 flags, sk_addr_t* from,
                    file_t** files, u32* nfiles, u32* out_flags) {
    bool nonblock = (f->flags & O_NONBLOCK) || (flags & SK_MSG_DONTWAIT);
    bool peek = flags & SK_MSG_PEEK;
    *nfiles = 0;
    while (!s->mhead) {
        if (s->shut_rd || (s->type == SK_SEQPACKET && (s->peer_gone || s->state != ST_CONNECTED))) return 0;
        if (nonblock) return -EAGAIN;
        sched_block_on(s);
        if (signal_pending(proc_current())) return -EINTR;
    }
    msg_t* m = s->mhead;
    u64 n = m->len < len ? m->len : len;
    memcpy(buf, m->data, n);
    if (m->len > len && out_flags) *out_flags |= SK_MSG_TRUNC;
    if (from) *from = m->from;
    if (files) {
        for (u32 i = 0; i < m->nfiles; i++) {
            files[i] = m->files[i];
            if (peek) file_ref(m->files[i]);
        }
        *nfiles = m->nfiles;
    } else if (!peek) {
        drop_files(m->files, m->nfiles);
    }
    if (!peek) {
        s->mhead = m->next;
        if (!s->mhead) s->mtail = NULL;
        s->mbytes -= m->len;
        kfree(m);
        wake(s);                                   // room for senders
    }
    return (i64)n;
}

i64 sock_send(file_t* f, const void* buf, u64 len, u32 flags, const sk_addr_t* to,
              file_t** files, u32 nfiles) {
    sock_t* s = S(f);
    if (s->type == SK_STREAM) {
        if (to && to->len && s->state != ST_CONNECTED) return -EOPNOTSUPP;
        return stream_send(f, s, buf, len, flags, files, nfiles);
    }
    return msg_send(f, s, buf, len, flags, to, files, nfiles);
}

i64 sock_recv(file_t* f, void* buf, u64 len, u32 flags, sk_addr_t* from,
              file_t** files, u32* nfiles, u32* out_flags) {
    sock_t* s = S(f);
    u32 dummy;
    if (!nfiles) nfiles = &dummy;
    if (s->state == ST_LISTEN) return -ENOTCONN;
    if (s->type == SK_STREAM) {
        if (from) memset(from, 0, sizeof(*from));
        // Without somewhere to put passed files, they are closed.
        file_t* local[SK_MAX_FDS];
        i64 r = stream_recv(f, s, buf, len, flags, files ? files : local, nfiles);
        if (!files) { drop_files(local, *nfiles); *nfiles = 0; }
        return r;
    }
    return msg_recv(f, s, buf, len, flags, from, files, nfiles, out_flags);
}

i32 sock_name(file_t* f, sk_addr_t* out) {
    *out = S(f)->name;
    return 0;
}

i32 sock_peer_name(file_t* f, sk_addr_t* out) {
    sock_t* s = S(f);
    if (s->peer) { *out = s->peer->name; return 0; }
    if (s->type == SK_DGRAM && s->dest.len) { *out = s->dest; return 0; }
    return -ENOTCONN;
}

i32 sock_peer_cred(file_t* f, sk_cred_t* out) {
    sock_t* s = S(f);
    if (s->state != ST_CONNECTED && !s->peer_gone) {
        // Linux answers for unconnected sockets with "nobody".
        out->pid = 0;
        out->uid = out->gid = 0xFFFFFFFF;
        return 0;
    }
    *out = s->peer_cred;
    return 0;
}

// ---- the open file ----

static i64 sock_read(file_t* f, void* buf, u64 len) {
    return sock_recv(f, buf, len, 0, NULL, NULL, NULL, NULL);
}

static i64 sock_write(file_t* f, const void* buf, u64 len) {
    return sock_send(f, buf, len, 0, NULL, NULL, 0);
}

static u32 sock_poll(file_t* f) {
    sock_t* s = S(f);
    u32 ev = 0;
    if (s->state == ST_LISTEN) return s->nbacklog ? POLLIN : 0;
    if (s->type == SK_STREAM) {
        if (s->rcount || s->batches || s->peer_gone || s->shut_rd) ev |= POLLIN;
        if (s->peer && s->peer->rcount < SK_BUF && !s->shut_wr) ev |= POLLOUT;
        if (s->peer_gone) ev |= POLLHUP;
        return ev;
    }
    if (s->mhead || s->shut_rd) ev |= POLLIN;
    if (s->type == SK_SEQPACKET) {
        if (s->peer && !s->shut_wr) ev |= POLLOUT;
        if (s->peer_gone) ev |= POLLHUP | POLLIN;
    } else {
        ev |= POLLOUT;
    }
    return ev;
}

static void sock_release(file_t* f) {
    sock_destroy(S(f));
}

static const file_ops_t sock_ops = {
    .read = sock_read,
    .write = sock_write,
    .poll = sock_poll,
    .release = sock_release,
};
