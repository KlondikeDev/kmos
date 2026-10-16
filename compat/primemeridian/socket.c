// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// Prime Meridian: Linux's socket system calls, onto kernel/socket.c.
//
// Only AF_UNIX exists so far (no networking yet): AF_INET and friends get
// EAFNOSUPPORT, which programs take to mean "no network here".

#include <types.h>
#include <socket.h>
#include <file.h>
#include <proc.h>
#include <usermode.h>
#include <memory.h>
#include <memset.h>
#include <memcpy.h>
#include <errno.h>
#include "linux.h"

#define AF_UNIX       1
#define EAFNOSUPPORT  97
#define EPROTONOSUPPORT 93
#define ENOTSOCK      88
#define ENOPROTOOPT   92

#define SOCK_TYPE_MASK 0xF
#define SOCK_NONBLOCK  04000
#define SOCK_CLOEXEC   02000000

#define SOL_SOCKET    1
#define SCM_RIGHTS    1
#define MSG_CTRUNC    0x8

#define SO_REUSEADDR  2
#define SO_TYPE       3
#define SO_ERROR      4
#define SO_SNDBUF     7
#define SO_RCVBUF     8
#define SO_PEERCRED   17
#define SO_ACCEPTCONN 30
#define SO_PROTOCOL   38
#define SO_DOMAIN     39

#define MAX_IOV_BYTES (1 * MB)

typedef struct {
    u64 msg_name;
    u32 msg_namelen;
    u32 __pad1;
    u64 msg_iov;
    u64 msg_iovlen;
    u64 msg_control;
    u64 msg_controllen;
    i32 msg_flags;
    u32 __pad2;
} l_msghdr_t;

typedef struct {
    u64 cmsg_len;
    i32 cmsg_level;
    i32 cmsg_type;
} l_cmsghdr_t;

#define CMSG_ALIGN(n) (((n) + 7) & ~7ULL)

static process_t* me(void) {
    return proc_current();
}

static bool uok(u64 a, u64 len, bool w) {
    return user_range_ok(a, len, w);
}

static file_t* sock_fd(u64 fd, i64* err) {
    file_t* f = fd_get(me(), (i32)fd);
    if (!f) { *err = -EBADF; return NULL; }
    if (!file_is_socket(f)) { *err = -ENOTSOCK; return NULL; }
    return f;
}

static i64 install(file_t* f, u32 flags) {
    i32 fd = fd_install(me(), f, 0);
    if (fd < 0) { file_unref(f); return fd; }
    if (flags & SOCK_CLOEXEC) me()->fd_cloexec |= 1u << fd;
    return fd;
}

// struct sockaddr_un in: family, then a path (to its NUL or the end) or,
// for the abstract namespace, a NUL and then the name's bytes.
static i64 addr_in(u64 uaddr, u64 len, sk_addr_t* a) {
    memset(a, 0, sizeof(*a));
    if (len < 2 || len > 2 + SK_PATH_MAX) return -EINVAL;
    if (!uok(uaddr, len, false)) return -EFAULT;
    const u8* p = (const u8*)uaddr;
    if ((p[0] | p[1] << 8) != AF_UNIX) return -EAFNOSUPPORT;
    const char* path = (const char*)p + 2;
    u64 n = len - 2;
    if (n == 0) return 0;                        // unnamed
    if (path[0] == '\0') {
        if (n < 2) return -EINVAL;
        a->abstract = true;
        a->len = (u32)(n - 1);
        memcpy(a->name, path + 1, a->len);
        return 0;
    }
    u32 k = 0;
    while (k < n && path[k]) k++;
    a->len = k;
    memcpy(a->name, path, k);
    return 0;
}

static i64 addr_out(const sk_addr_t* a, u64 uaddr, u64 ulen) {
    if (!uaddr) return 0;
    if (!uok(ulen, 4, true)) return -EFAULT;
    u32 cap = *(u32*)ulen;
    u8 tmp[2 + SK_PATH_MAX + 1];
    u32 n = 2;
    tmp[0] = AF_UNIX;
    tmp[1] = 0;
    if (a->len) {
        if (a->abstract) { tmp[n++] = 0; memcpy(tmp + n, a->name, a->len); n += a->len; }
        else { memcpy(tmp + n, a->name, a->len); n += a->len; tmp[n++] = 0; }
    }
    u32 copy = n < cap ? n : cap;
    if (copy && !uok(uaddr, copy, true)) return -EFAULT;
    memcpy((void*)uaddr, tmp, copy);
    *(u32*)ulen = n;                             // the full length, as Linux reports
    return 0;
}

static i64 l_socket(u64 domain, u64 type, u64 proto) {
    if (domain != AF_UNIX) return -EAFNOSUPPORT;
    if (proto != 0) return -EPROTONOSUPPORT;
    file_t* f;
    i32 r = sock_create((u32)(type & SOCK_TYPE_MASK), (type & SOCK_NONBLOCK) ? O_NONBLOCK : 0, &f);
    if (r < 0) return r == -EINVAL ? -EINVAL : r;
    return install(f, (u32)type);
}

static i64 l_socketpair(u64 domain, u64 type, u64 proto, u64 usv) {
    if (domain != AF_UNIX) return -EAFNOSUPPORT;
    if (proto != 0) return -EPROTONOSUPPORT;
    if (!uok(usv, 8, true)) return -EFAULT;
    file_t *a, *b;
    i32 r = sock_pair((u32)(type & SOCK_TYPE_MASK), (type & SOCK_NONBLOCK) ? O_NONBLOCK : 0, &a, &b);
    if (r < 0) return r;
    i64 fa = install(a, (u32)type);
    if (fa < 0) { file_unref(b); return fa; }
    i64 fb = install(b, (u32)type);
    if (fb < 0) { fd_close(me(), (i32)fa); return fb; }
    ((i32*)usv)[0] = (i32)fa;
    ((i32*)usv)[1] = (i32)fb;
    return 0;
}

static i64 l_bind_connect(u64 fd, u64 uaddr, u64 len, bool connect) {
    i64 err;
    file_t* f = sock_fd(fd, &err);
    if (!f) return err;
    sk_addr_t a;
    err = addr_in(uaddr, len, &a);
    if (err < 0) return err;
    return connect ? sock_connect(f, &a) : sock_bind(f, &a);
}

static i64 l_accept(u64 fd, u64 uaddr, u64 ulen, u64 flags) {
    i64 err;
    file_t* f = sock_fd(fd, &err);
    if (!f) return err;
    if (flags & ~(u64)(SOCK_NONBLOCK | SOCK_CLOEXEC)) return -EINVAL;
    file_t* nf;
    sk_addr_t peer;
    i32 r = sock_accept(f, (flags & SOCK_NONBLOCK) ? O_NONBLOCK : 0, &nf, &peer);
    if (r < 0) return r;
    i64 nfd = install(nf, (u32)flags);
    if (nfd >= 0) addr_out(&peer, uaddr, ulen);
    return nfd;
}

static i64 l_getname(u64 fd, u64 uaddr, u64 ulen, bool peer) {
    i64 err;
    file_t* f = sock_fd(fd, &err);
    if (!f) return err;
    sk_addr_t a;
    i32 r = peer ? sock_peer_name(f, &a) : sock_name(f, &a);
    if (r < 0) return r;
    return addr_out(&a, uaddr, ulen);
}

// Gather iovecs into one kernel buffer (or describe where to scatter).
static i64 iov_total(u64 uiov, u64 n, u64* total) {
    if (n > 1024) return -EINVAL;
    if (n && !uok(uiov, n * sizeof(l_iovec_t), false)) return -EFAULT;
    *total = 0;
    for (u64 i = 0; i < n; i++) *total += ((const l_iovec_t*)uiov)[i].iov_len;
    return 0;
}

static i64 do_send(file_t* f, u64 uiov, u64 niov, u32 flags, u64 uaddr, u64 alen,
                   file_t** files, u32 nfiles) {
    u64 total;
    i64 r = iov_total(uiov, niov, &total);
    if (r < 0) return r;
    if (total > MAX_IOV_BYTES) total = MAX_IOV_BYTES;
    u8* buf = kmalloc(total + 1);
    if (!buf) return -ENOMEM;
    u64 off = 0;
    for (u64 i = 0; i < niov && off < total; i++) {
        l_iovec_t v = ((const l_iovec_t*)uiov)[i];
        u64 n = v.iov_len < total - off ? v.iov_len : total - off;
        if (n && !uok(v.iov_base, n, false)) { kfree(buf); return -EFAULT; }
        memcpy(buf + off, (const void*)v.iov_base, n);
        off += n;
    }
    sk_addr_t to, *top = NULL;
    if (uaddr) {
        r = addr_in(uaddr, alen, &to);
        if (r < 0) { kfree(buf); return r; }
        top = &to;
    }
    r = sock_send(f, buf, total, flags, top, files, nfiles);
    kfree(buf);
    return r;
}

static i64 do_recv(file_t* f, u64 uiov, u64 niov, u32 flags, u64 uaddr, u64 ulen,
                   file_t** files, u32* nfiles, u32* out_flags) {
    u64 total;
    i64 r = iov_total(uiov, niov, &total);
    if (r < 0) return r;
    if (total > MAX_IOV_BYTES) total = MAX_IOV_BYTES;
    for (u64 i = 0; i < niov; i++) {
        l_iovec_t v = ((const l_iovec_t*)uiov)[i];
        if (v.iov_len && !uok(v.iov_base, v.iov_len, true)) return -EFAULT;
    }
    u8* buf = kmalloc(total + 1);
    if (!buf) return -ENOMEM;
    sk_addr_t from;
    memset(&from, 0, sizeof(from));
    r = sock_recv(f, buf, total, flags, &from, files, nfiles, out_flags);
    if (r > 0) {
        u64 off = 0;
        for (u64 i = 0; i < niov && off < (u64)r; i++) {
            l_iovec_t v = ((const l_iovec_t*)uiov)[i];
            u64 n = v.iov_len < (u64)r - off ? v.iov_len : (u64)r - off;
            memcpy((void*)v.iov_base, buf + off, n);
            off += n;
        }
    }
    kfree(buf);
    if (r >= 0 && uaddr) addr_out(&from, uaddr, ulen);
    return r;
}

static i64 l_sendto(u64 fd, u64 ubuf, u64 len, u64 flags, u64 uaddr, u64 alen) {
    i64 err;
    file_t* f = sock_fd(fd, &err);
    if (!f) return err;
    if (len && !uok(ubuf, len, false)) return -EFAULT;
    sk_addr_t to, *top = NULL;
    if (uaddr) {
        err = addr_in(uaddr, alen, &to);
        if (err < 0) return err;
        top = &to;
    }
    return sock_send(f, (const void*)ubuf, len, (u32)flags, top, NULL, 0);
}

static i64 l_recvfrom(u64 fd, u64 ubuf, u64 len, u64 flags, u64 uaddr, u64 ulen) {
    i64 err;
    file_t* f = sock_fd(fd, &err);
    if (!f) return err;
    if (len && !uok(ubuf, len, true)) return -EFAULT;
    sk_addr_t from;
    memset(&from, 0, sizeof(from));
    i64 r = sock_recv(f, (void*)ubuf, len, (u32)flags, &from, NULL, NULL, NULL);
    if (r >= 0 && uaddr) addr_out(&from, uaddr, ulen);
    return r;
}

static i64 l_sendmsg(u64 fd, u64 umsg, u64 flags) {
    i64 err;
    file_t* f = sock_fd(fd, &err);
    if (!f) return err;
    if (!uok(umsg, sizeof(l_msghdr_t), false)) return -EFAULT;
    l_msghdr_t m = *(const l_msghdr_t*)umsg;
    file_t* files[SK_MAX_FDS];
    u32 nfiles = 0;
    // Control messages: SCM_RIGHTS passes open files along.
    if (m.msg_control && m.msg_controllen) {
        if (!uok(m.msg_control, m.msg_controllen, false)) return -EFAULT;
        u64 off = 0;
        while (off + sizeof(l_cmsghdr_t) <= m.msg_controllen) {
            const l_cmsghdr_t* c = (const l_cmsghdr_t*)(m.msg_control + off);
            if (c->cmsg_len < sizeof(l_cmsghdr_t) || off + c->cmsg_len > m.msg_controllen) return -EINVAL;
            if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
                u64 n = (c->cmsg_len - sizeof(l_cmsghdr_t)) / 4;
                const i32* fds = (const i32*)(c + 1);
                for (u64 i = 0; i < n; i++) {
                    if (nfiles == SK_MAX_FDS) return -EINVAL;
                    file_t* g = fd_get(me(), fds[i]);
                    if (!g) return -EBADF;
                    files[nfiles++] = g;
                }
            }
            off += CMSG_ALIGN(c->cmsg_len);
        }
    }
    return do_send(f, m.msg_iov, m.msg_iovlen, (u32)flags, m.msg_name, m.msg_namelen, files, nfiles);
}

static i64 l_recvmsg(u64 fd, u64 umsg, u64 flags) {
    i64 err;
    file_t* f = sock_fd(fd, &err);
    if (!f) return err;
    if (!uok(umsg, sizeof(l_msghdr_t), true)) return -EFAULT;
    l_msghdr_t* m = (l_msghdr_t*)umsg;
    file_t* files[SK_MAX_FDS];
    u32 nfiles = 0, out_flags = 0;
    u32 namelen = m->msg_namelen;
    i64 r = do_recv(f, m->msg_iov, m->msg_iovlen, (u32)flags, m->msg_name,
                    m->msg_name ? (u64)&namelen : 0, files, &nfiles, &out_flags);
    if (r < 0) return r;
    m->msg_namelen = m->msg_name ? namelen : 0;

    // Files that came along become descriptors, in an SCM_RIGHTS message.
    u64 used = 0;
    if (nfiles) {
        u64 need = sizeof(l_cmsghdr_t) + 4 * (u64)nfiles;
        u64 room = m->msg_control ? m->msg_controllen : 0;
        u32 fit = room >= sizeof(l_cmsghdr_t) ? (u32)((room - sizeof(l_cmsghdr_t)) / 4) : 0;
        if (fit > nfiles) fit = nfiles;
        if (fit < nfiles) out_flags |= MSG_CTRUNC;
        if (fit && uok(m->msg_control, sizeof(l_cmsghdr_t) + 4 * fit, true)) {
            l_cmsghdr_t* c = (l_cmsghdr_t*)m->msg_control;
            i32* fds = (i32*)(c + 1);
            u32 placed = 0;
            for (u32 i = 0; i < fit; i++) {
                i32 nfd = fd_install(me(), files[i], 0);
                if (nfd < 0) { file_unref(files[i]); out_flags |= MSG_CTRUNC; continue; }
                if (flags & SK_MSG_CMSG_CLOEXEC) me()->fd_cloexec |= 1u << nfd;
                fds[placed++] = nfd;
            }
            c->cmsg_len = sizeof(l_cmsghdr_t) + 4 * placed;
            c->cmsg_level = SOL_SOCKET;
            c->cmsg_type = SCM_RIGHTS;
            used = CMSG_ALIGN(c->cmsg_len);
            for (u32 i = fit; i < nfiles; i++) file_unref(files[i]);
        } else {
            for (u32 i = 0; i < nfiles; i++) file_unref(files[i]);
        }
        (void)need;
    }
    m->msg_controllen = used;
    m->msg_flags = (i32)out_flags;
    return r;
}

static i64 l_getsockopt(u64 fd, u64 level, u64 name, u64 uval, u64 ulen) {
    i64 err;
    file_t* f = sock_fd(fd, &err);
    if (!f) return err;
    if (level != SOL_SOCKET) return -ENOPROTOOPT;
    if (!uok(ulen, 4, true)) return -EFAULT;
    u32 cap = *(u32*)ulen;
    u8 val[12];
    u32 n = 4;
    i32 iv = 0;
    switch (name) {
        case SO_TYPE:       iv = (i32)sock_type(f); break;
        case SO_ERROR:      iv = 0; break;
        case SO_SNDBUF:
        case SO_RCVBUF:     iv = 64 * 1024; break;
        case SO_ACCEPTCONN: iv = sock_listening(f); break;
        case SO_PROTOCOL:   iv = 0; break;
        case SO_DOMAIN:     iv = AF_UNIX; break;
        case SO_REUSEADDR:  iv = 0; break;
        case SO_PEERCRED: {
            sk_cred_t c;
            sock_peer_cred(f, &c);
            memcpy(val, &c.pid, 4);
            memcpy(val + 4, &c.uid, 4);
            memcpy(val + 8, &c.gid, 4);
            n = 12;
            break;
        }
        default: return -ENOPROTOOPT;
    }
    if (n == 4) memcpy(val, &iv, 4);
    u32 copy = n < cap ? n : cap;
    if (copy && !uok(uval, copy, true)) return -EFAULT;
    memcpy((void*)uval, val, copy);
    *(u32*)ulen = n;
    return 0;
}

static i64 l_setsockopt(u64 fd, u64 level, u64 name) {
    i64 err;
    file_t* f = sock_fd(fd, &err);
    if (!f) return err;
    (void)name;
    // Buffer sizes, address reuse, credential passing...: accepted, fixed.
    return level == SOL_SOCKET ? 0 : -ENOPROTOOPT;
}

// Called by the main table for the socket calls. False: not one of ours.
bool pm_socket_call(u64 nr, u64 a, u64 b, u64 c, u64 d, u64 e, u64 f6, i64* r) {
    switch (nr) {
        case 41:  *r = l_socket(a, b, c); return true;
        case 42:  *r = l_bind_connect(a, b, c, true); return true;
        case 43:  *r = l_accept(a, b, c, 0); return true;
        case 288: *r = l_accept(a, b, c, d); return true;
        case 44:  *r = l_sendto(a, b, c, d, e, f6); return true;
        case 45:  *r = l_recvfrom(a, b, c, d, e, f6); return true;
        case 46:  *r = l_sendmsg(a, b, c); return true;
        case 47:  *r = l_recvmsg(a, b, c); return true;
        case 48: {
            i64 err;
            file_t* fl = sock_fd(a, &err);
            *r = fl ? sock_shutdown(fl, (i32)b) : err;
            return true;
        }
        case 49:  *r = l_bind_connect(a, b, c, false); return true;
        case 50: {
            i64 err;
            file_t* fl = sock_fd(a, &err);
            *r = fl ? sock_listen(fl, (i32)b) : err;
            return true;
        }
        case 51:  *r = l_getname(a, b, c, false); return true;
        case 52:  *r = l_getname(a, b, c, true); return true;
        case 53:  *r = l_socketpair(a, b, c, d); return true;
        case 54:  *r = l_setsockopt(a, b, c); return true;
        case 55:  *r = l_getsockopt(a, b, c, d, e); return true;
        default:  return false;
    }
}
