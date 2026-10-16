// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// Prime Meridian's system call table: Linux x86_64 numbers and structures,
// served by the native kernel.
//
// Where Linux and Kunix already agree (read, write, lseek, dup2, mmap, the
// errno values, open flags, signal numbers, wait statuses) a call goes
// straight to the native handler. Otherwise this file translates: stat
// structures, directory entries, termios, the *at() family, signal masks.
//
// Kunix has no users or permissions yet, so every process is root and the
// ownership/mode calls succeed without effect. There are no threads, so
// clone() is fork() with a few extras and futexes never have waiters.
// Anything unlisted is -ENOSYS, and logged once per number to the serial
// port so missing pieces show up while bringing up new programs.

#include <types.h>
#include <primemeridian.h>
#include <ksys.h>
#include <proc.h>
#include <usermode.h>
#include <signal.h>
#include <file.h>
#include <vfs.h>
#include <devfs.h>
#include <tty.h>
#include <mman.h>
#include <memory.h>
#include <memset.h>
#include <memcpy.h>
#include <errno.h>
#include <rtc.h>
#include <ktime.h>
#include <pit.h>
#include <msr.h>
#include <gdt.h>
#include <serial.h>
#include <klibc/string.h>
#include "linux.h"

// errno values Kunix has no native use for yet (Linux numbering).
#define L_ELOOP         40
#define L_EOPNOTSUPP    95
#define L_EAFNOSUPPORT  97
#define L_ETIMEDOUT     110

#define PAGE_UP(x) (((x) + USER_PAGE_SIZE - 1) & ~(USER_PAGE_SIZE - 1))

static process_t* me(void) {
    return proc_current();
}

static bool uok(u64 addr, u64 len, bool write) {
    return user_range_ok(addr, len, write);
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

// Resolve a *at() path: absolute, relative to the working directory
// (AT_FDCWD), or relative to an open directory.
static i32 path_at(i64 dirfd, u64 upath, char* out) {
    char rel[VFS_PATH_MAX];
    i32 r = copy_path(rel, upath);
    if (r < 0) return r;
    if (rel[0] == '\0') return -ENOENT;
    if (rel[0] == '/' || (i32)dirfd == L_AT_FDCWD) {
        memcpy(out, rel, strlen(rel) + 1);
        return 0;
    }
    file_t* f = fd_get(me(), (i32)dirfd);
    if (!f) return -EBADF;
    if (!f->path || f->node->type != VN_DIR) return -ENOTDIR;
    u64 a = strlen(f->path), b = strlen(rel);
    if (a + 1 + b + 1 > VFS_PATH_MAX) return -ENAMETOOLONG;
    memcpy(out, f->path, a);
    out[a] = '/';
    memcpy(out + a + 1, rel, b + 1);
    return 0;
}

static i64 open_kpath(const char* kpath, u64 flags) {
    file_t* f;
    // O_PATH descriptors are only good for *at() and fstat: a read-only
    // open serves.
    if (flags & 010000000) flags = (flags & ~(010000000ULL | O_ACCMODE)) | O_RDONLY;
    i32 r = file_open(kpath, (u32)flags, &f);
    if (r < 0) return r;
    i32 fd = fd_install(me(), f, 0);
    if (fd < 0) {
        file_unref(f);
        return fd;
    }
    if (flags & O_CLOEXEC) me()->fd_cloexec |= 1u << fd;
    return fd;
}

// ---------------------------------------------------------------------------
// stat and friends
// ---------------------------------------------------------------------------

static void to_lstat(const vnode_t* n, l_stat_t* st) {
    kstat_t k;
    fill_stat(n, &k);
    memset(st, 0, sizeof(*st));
    st->st_dev = k.st_dev + 1;
    st->st_ino = k.st_ino ? k.st_ino : 1;
    st->st_nlink = n->type == VN_DIR ? 2 : 1;
    st->st_mode = k.st_mode;
    // No permissions yet: every regular file counts as executable, so
    // shells will run what PATH finds.
    if ((st->st_mode & S_IFMT) == S_IFREG) st->st_mode = S_IFREG | 0755;
    if (devfs_is_tty(n)) {
        st->st_mode = S_IFCHR | 0620;
        st->st_rdev = (4 << 8) | 1;                    // like /dev/tty1
    } else if (n->type == VN_CHR) {
        st->st_rdev = (1 << 8) | (u32)n->id;
    }
    st->st_size = (i64)k.st_size;
    st->st_atime = st->st_mtime = st->st_ctime = (i64)n->mtime;
    st->st_blksize = 4096;
    st->st_blocks = (i64)((k.st_size + 511) / 512);
}

static i64 stat_kpath(const char* kpath, u64 out) {
    if (!uok(out, sizeof(l_stat_t), true)) return -EFAULT;
    vnode_t n;
    i32 r = vfs_lookup(kpath, &n);
    if (r < 0) return r;
    to_lstat(&n, (l_stat_t*)out);
    return 0;
}

static i64 l_fstat(u64 fd, u64 out) {
    file_t* f = fd_get(me(), (i32)fd);
    if (!f) return -EBADF;
    if (!uok(out, sizeof(l_stat_t), true)) return -EFAULT;
    to_lstat(f->node, (l_stat_t*)out);
    return 0;
}

static i64 l_newfstatat(i64 dirfd, u64 path, u64 out, u64 flags) {
    if (flags & L_AT_EMPTY_PATH) {
        char p[2];
        if (path && copy_string_from_user(p, path, 2) && p[0] == '\0') return l_fstat((u64)dirfd, out);
    }
    char kpath[VFS_PATH_MAX];
    i32 r = path_at(dirfd, path, kpath);
    if (r < 0) return r;
    return stat_kpath(kpath, out);
}

static i64 l_statfs_fill(const vnode_t* n, u64 out) {
    if (!uok(out, sizeof(l_statfs_t), true)) return -EFAULT;
    l_statfs_t s;
    memset(&s, 0, sizeof(s));
    const char* type = n->mnt && n->mnt->ops ? n->mnt->ops->type : "";
    if (!strcmp(type, "fat32")) s.f_type = 0x4d44;          // MSDOS_SUPER_MAGIC
    else if (!strcmp(type, "ufs2")) s.f_type = 0x19540119;  // UFS2_MAGIC
    else s.f_type = 0x1373;                                 // DEVFS
    s.f_bsize = s.f_frsize = 4096;
    s.f_namelen = 255;
    memcpy((void*)out, &s, sizeof(s));
    return 0;
}

static i64 l_statfs(u64 path, u64 out) {
    char kpath[VFS_PATH_MAX];
    i32 r = copy_path(kpath, path);
    if (r < 0) return r;
    vnode_t n;
    r = vfs_lookup(kpath, &n);
    if (r < 0) return r;
    return l_statfs_fill(&n, out);
}

static i64 l_fstatfs(u64 fd, u64 out) {
    file_t* f = fd_get(me(), (i32)fd);
    if (!f) return -EBADF;
    return l_statfs_fill(f->node, out);
}

// The path must exist; then the call is a no-op (no permissions, no
// timestamps to set yet).
static i64 exists_at(i64 dirfd, u64 path) {
    char kpath[VFS_PATH_MAX];
    i32 r = path_at(dirfd, path, kpath);
    if (r < 0) return r;
    vnode_t n;
    return vfs_lookup(kpath, &n);
}

static i64 l_readlinkat(i64 dirfd, u64 path) {
    // Nothing is a symlink: an existing path is "not a link".
    i64 r = exists_at(dirfd, path);
    return r < 0 ? r : -EINVAL;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

static i64 l_getdents64(u64 fd, u64 buf, u64 count) {
    file_t* f = fd_get(me(), (i32)fd);
    if (!f) return -EBADF;
    if (f->node->type != VN_DIR || !f->path) return -ENOTDIR;
    if (!uok(buf, count, true)) return -EFAULT;

    // Positions 0 and 1 are "." and "..", as Linux lists them; the
    // filesystem's own entries follow.
    u64 off = 0;
    for (;;) {
        char name[VFS_NAME_MAX];
        vnode_t n;
        i32 r;
        if (f->pos < 2) {
            name[0] = '.';
            name[1] = f->pos ? '.' : '\0';
            name[2] = '\0';
            n = *f->node;
            r = 1;
        } else {
            r = vfs_readdir_path(f->path, (u32)(f->pos - 2), name, &n);
        }
        if (r < 0) return off ? (i64)off : r;
        if (r == 0) break;
        u64 len = strlen(name);
        u64 reclen = (sizeof(l_dirent64_t) + len + 1 + 7) & ~7ULL;
        if (off + reclen > count) {
            if (off == 0) return -EINVAL;              // buffer too small for one
            break;
        }
        l_dirent64_t* d = (l_dirent64_t*)(buf + off);
        d->d_ino = n.id ? n.id : f->pos + 1;
        d->d_off = (i64)f->pos + 1;
        d->d_reclen = (u16)reclen;
        d->d_type = n.type == VN_DIR ? L_DT_DIR : n.type == VN_CHR ? L_DT_CHR :
                    n.type == VN_FIFO ? L_DT_FIFO : L_DT_REG;
        memcpy(d->d_name, name, len + 1);
        f->pos++;
        off += reclen;
    }
    return (i64)off;
}

static i64 l_rw_vec(u64 fd, u64 iov, u64 n, bool write) {
    if (n > 1024) return -EINVAL;
    if (!uok(iov, n * sizeof(l_iovec_t), false)) return -EFAULT;
    i64 total = 0;
    for (u64 i = 0; i < n; i++) {
        l_iovec_t v = ((const l_iovec_t*)iov)[i];
        if (v.iov_len == 0) continue;
        i64 r = write ? sys_write(fd, v.iov_base, v.iov_len) : sys_read(fd, v.iov_base, v.iov_len);
        if (r < 0) return total ? total : r;
        total += r;
        if ((u64)r < v.iov_len) break;                 // short: stop here
    }
    return total;
}

static i64 l_pread(u64 fd, u64 buf, u64 len, i64 off, bool write) {
    file_t* f = fd_get(me(), (i32)fd);
    if (!f) return -EBADF;
    if (f->node->type != VN_REG) return -ESPIPE;
    if (off < 0) return -EINVAL;
    if (!uok(buf, len, !write)) return -EFAULT;
    u32 acc = f->flags & O_ACCMODE;
    if (write ? acc == O_RDONLY : acc == O_WRONLY) return -EBADF;
    return write ? vfs_write(f->node, (u64)off, (const void*)buf, len)
                 : vfs_read(f->node, (u64)off, (void*)buf, len);
}

// Copy between descriptors in the kernel (cat and cp use it).
static i64 l_sendfile(u64 out_fd, u64 in_fd, u64 uoff, u64 count) {
    file_t* in = fd_get(me(), (i32)in_fd);
    file_t* out = fd_get(me(), (i32)out_fd);
    if (!in || !out) return -EBADF;
    if (uoff && !uok(uoff, 8, true)) return -EFAULT;
    i64 off = uoff ? *(i64*)uoff : -1;
    if (uoff && (off < 0 || in->node->type != VN_REG)) return off < 0 ? -EINVAL : -ESPIPE;
    if (count > 0x7FFFF000) count = 0x7FFFF000;
    u8* buf = kmalloc(4096);
    if (!buf) return -ENOMEM;
    i64 total = 0;
    while ((u64)total < count) {
        u64 chunk = count - (u64)total < 4096 ? count - (u64)total : 4096;
        i64 n = off >= 0 ? vfs_read(in->node, (u64)off, buf, chunk) : file_read(in, buf, chunk);
        if (n <= 0) { if (n < 0 && !total) total = n; break; }
        i64 w = file_write(out, buf, (u64)n);
        if (w < 0) { if (!total) total = w; break; }
        total += w;
        if (off >= 0) off += w;
        if (w < n) break;
    }
    kfree(buf);
    if (uoff && off >= 0) *(i64*)uoff = off;
    return total;
}

static i64 l_fcntl(u64 fd, u64 cmd, u64 arg) {
    process_t* p = me();
    file_t* f = fd_get(p, (i32)fd);
    if (!f) return -EBADF;
    switch (cmd) {
        case L_F_DUPFD:
        case L_F_DUPFD_CLOEXEC: {
            if (arg >= MAX_FDS) return -EINVAL;
            i32 nfd = fd_install(p, f, (i32)arg);
            if (nfd < 0) return nfd;
            file_ref(f);
            if (cmd == L_F_DUPFD_CLOEXEC) p->fd_cloexec |= 1u << nfd;
            return nfd;
        }
        case L_F_GETFD: return (p->fd_cloexec >> fd) & 1 ? L_FD_CLOEXEC : 0;
        case L_F_SETFD:
            if (arg & L_FD_CLOEXEC) p->fd_cloexec |= 1u << fd;
            else p->fd_cloexec &= ~(1u << fd);
            return 0;
        case L_F_GETFL: return f->flags & (O_ACCMODE | O_APPEND | O_NONBLOCK);
        case L_F_SETFL:
            f->flags = (f->flags & ~(u32)(O_APPEND | O_NONBLOCK)) | (u32)(arg & (O_APPEND | O_NONBLOCK));
            return 0;
        case L_F_GETLK:
            // No locks are ever held: report the region as unlocked.
            if (!uok(arg, 2, true)) return -EFAULT;
            *(i16*)arg = 2;                            // F_UNLCK
            return 0;
        case L_F_SETLK:
        case L_F_SETLKW:
            return 0;
        default:
            return -EINVAL;
    }
}

static i64 l_pipe2(u64 fds, u64 flags) {
    if (flags & ~(u64)(O_CLOEXEC | O_NONBLOCK)) return -EINVAL;
    i64 r = sys_pipe(fds);
    if (r < 0) return r;
    process_t* p = me();
    for (int i = 0; i < 2; i++) {
        i32 fd = ((i32*)fds)[i];
        if (flags & O_CLOEXEC) p->fd_cloexec |= 1u << fd;
        if (flags & O_NONBLOCK) p->fds[fd]->flags |= O_NONBLOCK;
    }
    return 0;
}

static i64 l_dup3(u64 oldfd, u64 newfd, u64 flags) {
    if (oldfd == newfd) return -EINVAL;
    if (flags & ~(u64)O_CLOEXEC) return -EINVAL;
    i64 r = sys_dup2(oldfd, newfd);
    if (r >= 0 && (flags & O_CLOEXEC)) me()->fd_cloexec |= 1u << newfd;
    return r;
}

static i64 l_truncate(u64 path, i64 len) {
    char kpath[VFS_PATH_MAX];
    i32 r = copy_path(kpath, path);
    if (r < 0) return r;
    if (len < 0) return -EINVAL;
    file_t* f;
    r = file_open(kpath, O_WRONLY, &f);
    if (r < 0) return r;
    r = vfs_truncate(f->node, (u64)len);
    file_unref(f);
    return r;
}

static i64 l_fchdir(u64 fd) {
    file_t* f = fd_get(me(), (i32)fd);
    if (!f) return -EBADF;
    if (f->node->type != VN_DIR || !f->path) return -ENOTDIR;
    return vfs_chdir(f->path);
}

// ---------------------------------------------------------------------------
// Terminals
// ---------------------------------------------------------------------------

static i64 l_ioctl(u64 fd, u64 req, u64 arg) {
    process_t* p = me();
    file_t* f = fd_get(p, (i32)fd);
    if (!f) return -EBADF;

    switch (req) {                                     // any descriptor
        case L_FIOCLEX:  p->fd_cloexec |= 1u << fd; return 0;
        case L_FIONCLEX: p->fd_cloexec &= ~(1u << fd); return 0;
        case L_FIONBIO:
            if (!uok(arg, 4, false)) return -EFAULT;
            if (*(i32*)arg) f->flags |= O_NONBLOCK;
            else f->flags &= ~(u32)O_NONBLOCK;
            return 0;
    }

    if (!devfs_is_tty(f->node)) return -ENOTTY;
    switch (req) {
        case L_TCGETS:
            if (!uok(arg, sizeof(tty_termios_t), true)) return -EFAULT;
            tty_get_termios((tty_termios_t*)arg);
            return 0;
        case L_TCSETSF:
            tty_flush_input();
            // fall through
        case L_TCSETS:
        case L_TCSETSW: {
            if (!uok(arg, sizeof(tty_termios_t), false)) return -EFAULT;
            tty_termios_t t;
            memcpy(&t, (const void*)arg, sizeof(t));
            tty_set_termios(&t);
            return 0;
        }
        case L_TIOCGWINSZ: {
            if (!uok(arg, sizeof(l_winsize_t), true)) return -EFAULT;
            l_winsize_t* w = (l_winsize_t*)arg;
            tty_get_winsize(&w->ws_row, &w->ws_col);
            w->ws_xpixel = (u16)(w->ws_col * 8);
            w->ws_ypixel = (u16)(w->ws_row * 16);
            return 0;
        }
        case L_TIOCSWINSZ: return 0;                   // the console's size is fixed
        case L_TIOCGPGRP:
            if (!uok(arg, 4, true)) return -EFAULT;
            *(i32*)arg = tty_get_foreground();
            return 0;
        case L_TIOCSPGRP:
            if (!uok(arg, 4, false)) return -EFAULT;
            tty_set_foreground(*(i32*)arg);
            return 0;
        case L_TIOCGSID:
            if (!uok(arg, 4, true)) return -EFAULT;
            *(i32*)arg = p->sid;
            return 0;
        case L_FIONREAD:
            if (!uok(arg, 4, true)) return -EFAULT;
            *(i32*)arg = (i32)tty_bytes_ready();
            return 0;
        case L_TIOCOUTQ:
            if (!uok(arg, 4, true)) return -EFAULT;
            *(i32*)arg = 0;                            // output is never queued
            return 0;
        case L_TCFLSH:
            if (arg == 0 || arg == 2) tty_flush_input();
            return 0;
        case L_TIOCSCTTY:
        case L_TIOCNOTTY:
        case L_TCSBRK:
        case L_TCXONC:
            return 0;
        default:
            return -ENOTTY;
    }
}

// ---------------------------------------------------------------------------
// poll and select
// ---------------------------------------------------------------------------

#define POLL_MAX 256

// Poll kernel copies of the pollfds. timeout_ms < 0 waits for ever.
static i64 do_poll(l_pollfd_t* fds, u64 n, i64 timeout_ms) {
    process_t* p = me();
    u32 deadline = 0;
    if (timeout_ms > 0) deadline = ticks + (u32)((timeout_ms + 3) / 4);
    for (;;) {
        i64 ready = 0;
        for (u64 i = 0; i < n; i++) {
            fds[i].revents = 0;
            if (fds[i].fd < 0) continue;
            file_t* f = fd_get(p, fds[i].fd);
            if (!f) { fds[i].revents = POLLNVAL; ready++; continue; }
            u32 ev = file_poll(f) & ((u16)fds[i].events | POLLHUP | POLLERR);
            fds[i].revents = (i16)ev;
            if (ev) ready++;
        }
        if (ready || timeout_ms == 0) return ready;
        if (signal_pending(p)) return -EINTR;
        if (timeout_ms < 0) {
            sched_block_on(&file_poll_wait);
        } else if (!sched_block_on_until(&file_poll_wait, deadline)) {
            timeout_ms = 0;                            // one last look, then 0
        }
    }
}

static i64 l_poll(u64 ufds, u64 n, i64 timeout_ms) {
    if (n > POLL_MAX) return -EINVAL;
    if (!uok(ufds, n * sizeof(l_pollfd_t), true)) return -EFAULT;
    l_pollfd_t* k = kmalloc(n * sizeof(l_pollfd_t) + 1);
    if (!k) return -ENOMEM;
    memcpy(k, (const void*)ufds, n * sizeof(l_pollfd_t));
    i64 r = do_poll(k, n, timeout_ms);
    if (r >= 0) {
        for (u64 i = 0; i < n; i++) ((l_pollfd_t*)ufds)[i].revents = k[i].revents;
    }
    kfree(k);
    return r;
}

// Install a temporary signal mask for the length of a call (ppoll,
// pselect6, sigsuspend). pm_syscall puts the old one back afterwards --
// after any handler, whose frame records the old mask.
static i64 swap_mask(u64 usigset, u64 size) {
    if (!usigset) return 0;
    if (size != 8) return -EINVAL;
    if (!uok(usigset, 8, false)) return -EFAULT;
    process_t* p = me();
    p->sig_saved_mask = p->sig_mask;
    p->sig_mask_saved = true;
    p->sig_mask = l_sigset_to_mask(*(const u64*)usigset) & ~((1u << SIGKILL) | (1u << SIGSTOP));
    return 0;
}

static i64 timespec_ms(u64 uts, i64* out) {
    if (!uts) { *out = -1; return 0; }
    if (!uok(uts, sizeof(l_timespec_t), false)) return -EFAULT;
    l_timespec_t ts = *(const l_timespec_t*)uts;
    if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000) return -EINVAL;
    *out = ts.tv_sec * 1000 + (ts.tv_nsec + 999999) / 1000000;
    return 0;
}

static i64 l_ppoll(u64 ufds, u64 n, u64 uts, u64 sigmask, u64 size) {
    i64 ms;
    i64 r = timespec_ms(uts, &ms);
    if (r < 0) return r;
    r = swap_mask(sigmask, size);
    if (r < 0) return r;
    return l_poll(ufds, n, ms);
}

// select() on top of poll: fd_sets in, pollfds through, fd_sets out.
static i64 do_select(u64 nfds, u64 rset, u64 wset, u64 eset, i64 timeout_ms) {
    if (nfds > 1024) return -EINVAL;
    u64 bytes = (nfds + 63) / 64 * 8;
    if ((rset && !uok(rset, bytes, true)) || (wset && !uok(wset, bytes, true)) ||
        (eset && !uok(eset, bytes, true))) return -EFAULT;
    if (nfds > MAX_FDS) nfds = MAX_FDS;                // nothing can be open above that

    l_pollfd_t k[MAX_FDS];
    u32 n = 0;
    for (u32 fd = 0; fd < nfds; fd++) {
        i16 ev = 0;
        if (rset && (((const u64*)rset)[fd / 64] >> (fd % 64) & 1)) ev |= POLLIN;
        if (wset && (((const u64*)wset)[fd / 64] >> (fd % 64) & 1)) ev |= POLLOUT;
        if (eset && (((const u64*)eset)[fd / 64] >> (fd % 64) & 1)) ev |= POLLPRI;
        if (!ev) continue;
        if (!fd_get(me(), (i32)fd)) return -EBADF;
        k[n].fd = (i32)fd;
        k[n].events = ev;
        n++;
    }
    i64 r = do_poll(k, n, timeout_ms);
    if (r < 0) return r;
    if (rset) memset((void*)rset, 0, (u32)bytes);
    if (wset) memset((void*)wset, 0, (u32)bytes);
    if (eset) memset((void*)eset, 0, (u32)bytes);
    i64 count = 0;
    for (u32 i = 0; i < n; i++) {
        u32 fd = (u32)k[i].fd;
        u64 bit = 1ULL << (fd % 64);
        if (rset && (k[i].revents & (POLLIN | POLLHUP | POLLERR)) && (k[i].events & POLLIN)) {
            ((u64*)rset)[fd / 64] |= bit; count++;
        }
        if (wset && (k[i].revents & (POLLOUT | POLLERR)) && (k[i].events & POLLOUT)) {
            ((u64*)wset)[fd / 64] |= bit; count++;
        }
        if (eset && (k[i].revents & POLLPRI)) {
            ((u64*)eset)[fd / 64] |= bit; count++;
        }
    }
    return count;
}

static i64 l_select(u64 nfds, u64 r, u64 w, u64 e, u64 utv) {
    i64 ms = -1;
    if (utv) {
        if (!uok(utv, sizeof(l_timeval_t), false)) return -EFAULT;
        l_timeval_t tv = *(const l_timeval_t*)utv;
        if (tv.tv_sec < 0 || tv.tv_usec < 0) return -EINVAL;
        ms = tv.tv_sec * 1000 + (tv.tv_usec + 999) / 1000;
    }
    return do_select(nfds, r, w, e, ms);
}

static i64 l_pselect6(u64 nfds, u64 r, u64 w, u64 e, u64 uts, u64 usig) {
    i64 ms;
    i64 err = timespec_ms(uts, &ms);
    if (err < 0) return err;
    if (usig) {
        // A {sigset pointer, size} pair.
        if (!uok(usig, 16, false)) return -EFAULT;
        err = swap_mask(((const u64*)usig)[0], ((const u64*)usig)[1]);
        if (err < 0) return err;
    }
    return do_select(nfds, r, w, e, ms);
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

static u64 uptime_ms(void) {
    return (u64)ticks * 4;                             // PIT at 250 Hz
}

static i64 l_clock_gettime(u64 clk, u64 uts) {
    if (!uok(uts, sizeof(l_timespec_t), true)) return -EFAULT;
    u64 ns;
    switch (clk) {
        case L_CLOCK_REALTIME:
        case L_CLOCK_REALTIME_COARSE:
            ns = time_real_ns();
            break;
        case L_CLOCK_MONOTONIC:
        case L_CLOCK_MONOTONIC_RAW:
        case L_CLOCK_MONOTONIC_COARSE:
        case L_CLOCK_BOOTTIME:
            ns = time_boot_ns();
            break;
        case L_CLOCK_PROCESS_CPUTIME_ID:
        case L_CLOCK_THREAD_CPUTIME_ID:
            ns = (u64)me()->cpu_ticks * 4000000ULL;
            break;
        default:
            return -EINVAL;
    }
    l_timespec_t* ts = (l_timespec_t*)uts;
    ts->tv_sec = (i64)(ns / 1000000000ULL);
    ts->tv_nsec = (i64)(ns % 1000000000ULL);
    return 0;
}

static i64 l_gettimeofday(u64 utv, u64 utz) {
    if (utv) {
        if (!uok(utv, sizeof(l_timeval_t), true)) return -EFAULT;
        u64 us = time_real_ns() / 1000;
        ((l_timeval_t*)utv)->tv_sec = (i64)(us / 1000000);
        ((l_timeval_t*)utv)->tv_usec = (i64)(us % 1000000);
    }
    if (utz) {
        if (!uok(utz, 8, true)) return -EFAULT;
        memset((void*)utz, 0, 8);                     // UTC, no DST
    }
    return 0;
}

// Sleep for ms; on a signal store what was left in urem.
static i64 sleep_ms(u64 ms, u64 urem) {
    u32 n = (u32)((ms + 3) / 4);
    u32 deadline = ticks + n;
    if (n) sched_sleep(n);
    i32 left = (i32)(deadline - ticks);
    if (left > 0 && signal_pending(me())) {
        if (urem && uok(urem, sizeof(l_timespec_t), true)) {
            u64 lms = (u64)left * 4;
            ((l_timespec_t*)urem)->tv_sec = (i64)(lms / 1000);
            ((l_timespec_t*)urem)->tv_nsec = (i64)(lms % 1000) * 1000000;
        }
        return -EINTR;
    }
    return 0;
}

static i64 l_nanosleep(u64 ureq, u64 urem) {
    i64 ms;
    if (!ureq) return -EFAULT;
    i64 r = timespec_ms(ureq, &ms);
    if (r < 0) return r;
    return sleep_ms((u64)ms, urem);
}

static i64 l_clock_nanosleep(u64 clk, u64 flags, u64 ureq, u64 urem) {
    i64 ms;
    if (!ureq) return -EFAULT;
    i64 r = timespec_ms(ureq, &ms);
    if (r < 0) return r;
    if (flags & 1) {                                   // TIMER_ABSTIME
        u64 now = (clk == L_CLOCK_REALTIME ? time_real_ns() : time_boot_ns()) / 1000000;
        ms = (u64)ms > now ? ms - (i64)now : 0;
        urem = 0;
    }
    return sleep_ms((u64)ms, urem);
}

static i64 l_setitimer(u64 which, u64 unew, u64 uold) {
    if (which != 0) return -EINVAL;                    // ITIMER_REAL only
    process_t* p = me();
    if (uold) {
        if (!uok(uold, sizeof(l_itimerval_t), true)) return -EFAULT;
        l_itimerval_t* o = (l_itimerval_t*)uold;
        memset(o, 0, sizeof(*o));
        if (p->alarm_set) {
            u64 left = (u64)(i32)(p->alarm_tick - ticks) * 4;
            o->it_value.tv_sec = (i64)(left / 1000);
            o->it_value.tv_usec = (i64)(left % 1000) * 1000;
            o->it_interval.tv_sec = (i64)(p->alarm_interval * 4 / 1000);
            o->it_interval.tv_usec = (i64)(p->alarm_interval * 4 % 1000) * 1000;
        }
    }
    if (unew) {
        if (!uok(unew, sizeof(l_itimerval_t), false)) return -EFAULT;
        l_itimerval_t v = *(const l_itimerval_t*)unew;
        u64 value = (u64)v.it_value.tv_sec * 1000 + (u64)v.it_value.tv_usec / 1000;
        u64 interval = (u64)v.it_interval.tv_sec * 1000 + (u64)v.it_interval.tv_usec / 1000;
        if (value == 0 && v.it_value.tv_usec == 0) {
            p->alarm_set = false;
        } else {
            p->alarm_tick = ticks + (u32)((value + 3) / 4 ? (value + 3) / 4 : 1);
            p->alarm_interval = (u32)((interval + 3) / 4);
            p->alarm_set = true;
        }
    }
    return 0;
}

static i64 l_alarm(u64 secs) {
    process_t* p = me();
    i64 left = p->alarm_set ? ((i64)(i32)(p->alarm_tick - ticks) + 249) / 250 : 0;
    if (secs) {
        p->alarm_tick = ticks + (u32)(secs * 250);
        p->alarm_interval = 0;
        p->alarm_set = true;
    } else {
        p->alarm_set = false;
    }
    return left;
}

// ---------------------------------------------------------------------------
// Processes
// ---------------------------------------------------------------------------

static i64 l_wait4(i64 pid, u64 status, u64 options, u64 rusage) {
    if (rusage) {
        if (!uok(rusage, sizeof(l_rusage_t), true)) return -EFAULT;
        memset((void*)rusage, 0, sizeof(l_rusage_t));
    }
    if (pid == 0) pid = -(i64)me()->pgid;              // our own process group
    return sys_waitpid((u64)pid, status, options & (WNOHANG | WUNTRACED));
}

static i64 l_kill(i64 pid, u64 sig) {
    if (sig >= NSIG) return -EINVAL;
    if (pid == 0) pid = -(i64)me()->pgid;
    if (pid == -1) {
        // Everyone we may signal: all but the kernel, init and ourselves.
        i32 n = 0;
        for (u32 i = 1; i < MAX_PROCS; i++) {
            process_t* p = proc_slot(i);
            if (p->state == PROC_UNUSED || p->state == PROC_ZOMBIE || p->pid <= 1 || p == me()) continue;
            if (sig) signal_send(p, (i32)sig);
            n++;
        }
        return n ? 0 : -ESRCH;
    }
    return sys_kill(pid, sig);
}

// clone(): no threads, so only process-shaped requests. CLONE_VM|CLONE_VFORK
// (posix_spawn, vfork) is safe to run as a fork onto the given stack: the
// child only reads what its parent set up there before execve'ing.
#define L_CLONE_VM             0x00000100
#define L_CLONE_FS             0x00000200
#define L_CLONE_FILES          0x00000400
#define L_CLONE_SIGHAND        0x00000800
#define L_CLONE_VFORK          0x00004000
#define L_CLONE_THREAD         0x00010000
#define L_CLONE_SETTLS         0x00080000
#define L_CLONE_PARENT_SETTID  0x00100000
#define L_CLONE_CHILD_CLEARTID 0x00200000
#define L_CLONE_CHILD_SETTID   0x01000000

static i64 l_clone(registers_t* regs, u64 flags, u64 stack, u64 ptid, u64 ctid, u64 tls) {
    (void)ctid;
    if (flags & (L_CLONE_THREAD | L_CLONE_SIGHAND)) return -ENOSYS;
    if ((flags & L_CLONE_VM) && !(flags & L_CLONE_VFORK)) return -ENOSYS;
    registers_t frame = *regs;
    if (stack) frame.rsp = stack;
    process_t* child = proc_fork(&frame);
    if (!child) return -ENOMEM;
    if (flags & L_CLONE_SETTLS) child->fs_base = tls;
    if ((flags & L_CLONE_PARENT_SETTID) && uok(ptid, 4, true)) *(i32*)ptid = child->pid;
    return child->pid;
}

static i64 l_setsid(void) {
    process_t* p = me();
    if (p->pgid == p->pid) return -EPERM;              // already a group leader
    p->sid = p->pgid = p->pid;
    return p->pid;
}

static i64 l_getsid(i64 pid) {
    process_t* p = pid == 0 ? me() : proc_by_pid((i32)pid);
    if (!p) return -ESRCH;
    return p->sid;
}

static void put_field(char* dst, const char* src) {
    u32 i = 0;
    for (; src[i] && i < 64; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static i64 l_uname(u64 out) {
    if (!uok(out, sizeof(l_utsname_t), true)) return -EFAULT;
    l_utsname_t* u = (l_utsname_t*)out;
    memset(u, 0, sizeof(*u));
    // Linux programs are told they run on Linux -- that is the point of a
    // personality -- with the release naming who is actually serving them.
    put_field(u->sysname, "Linux");
    put_field(u->nodename, "kunix");
    put_field(u->release, "6.1.0-primemeridian");
    put_field(u->version, "Kunix Millennium Operating System (Prime Meridian)");
    put_field(u->machine, "x86_64");
    put_field(u->domainname, "(none)");
    return 0;
}

static i64 l_prlimit(u64 resource, u64 unew, u64 uold) {
    (void)unew;                                        // limits are fixed; accept and ignore
    if (uold) {
        if (!uok(uold, sizeof(l_rlimit_t), true)) return -EFAULT;
        l_rlimit_t* r = (l_rlimit_t*)uold;
        r->rlim_cur = r->rlim_max = L_RLIM_INFINITY;
        if (resource == L_RLIMIT_STACK) r->rlim_cur = 8 * MB;
        if (resource == L_RLIMIT_NOFILE) r->rlim_cur = r->rlim_max = MAX_FDS;
    }
    return 0;
}

static i64 l_sysinfo(u64 out) {
    if (!uok(out, sizeof(l_sysinfo_t), true)) return -EFAULT;
    l_sysinfo_t* s = (l_sysinfo_t*)out;
    memset(s, 0, sizeof(*s));
    s->uptime = (i64)(uptime_ms() / 1000);
    s->totalram = pmm_total_pages() * 4096;
    s->freeram = pmm_count_free() * 4096;
    u16 n = 0;
    for (u32 i = 1; i < MAX_PROCS; i++) if (proc_slot(i)->state != PROC_UNUSED) n++;
    s->procs = n;
    s->mem_unit = 1;
    return 0;
}

static i64 l_times(u64 out) {
    if (out) {
        if (!uok(out, sizeof(l_tms_t), true)) return -EFAULT;
        memset((void*)out, 0, sizeof(l_tms_t));        // no CPU accounting yet
    }
    return (i64)((u64)ticks * 100 / 250);             // in CLK_TCK (100 Hz) units
}

static i64 l_prctl(u64 option, u64 arg) {
    process_t* p = me();
    if (option == L_PR_SET_NAME) {
        char name[16];
        if (!copy_string_from_user(name, arg, sizeof(name))) {
            if (!uok(arg, 16, false)) return -EFAULT;
            memcpy(name, (const void*)arg, 15);
            name[15] = '\0';
        }
        u32 i = 0;
        for (; name[i] && i < PROC_NAME_LEN - 1; i++) p->name[i] = name[i];
        p->name[i] = '\0';
        return 0;
    }
    if (option == L_PR_GET_NAME) {
        if (!uok(arg, 16, true)) return -EFAULT;
        memset((void*)arg, 0, 16);
        for (u32 i = 0; i < 15 && p->name[i]; i++) ((char*)arg)[i] = p->name[i];
        return 0;
    }
    return -EINVAL;
}

static i64 l_arch_prctl(u64 code, u64 addr) {
    process_t* p = me();
    switch (code) {
        case L_ARCH_SET_FS:
            if (addr >= USER_STACK_TOP) return -EPERM;
            p->fs_base = addr;
            wrmsr(MSR_FS_BASE, addr);
            return 0;
        case L_ARCH_GET_FS:
            if (!uok(addr, 8, true)) return -EFAULT;
            *(u64*)addr = p->fs_base;
            return 0;
        default:
            return -EINVAL;                            // GS is the kernel's
    }
}

static i64 l_futex(u64 uaddr, u64 op, u64 val, u64 utimeout) {
    switch (op & 127) {
        case L_FUTEX_WAIT: {
            if (!uok(uaddr, 4, false)) return -EFAULT;
            if (*(volatile u32*)uaddr != (u32)val) return -EAGAIN;
            // With no other threads, nobody will ever wake us: only a
            // signal or the timeout ends this.
            i64 ms;
            i64 r = timespec_ms(utimeout, &ms);
            if (r < 0) return r;
            static int nobody;
            if (ms < 0) {
                sched_block_on(&nobody);
                return -EINTR;
            }
            if (sched_block_on_until(&nobody, ticks + (u32)((ms + 3) / 4))) return -EINTR;
            return -L_ETIMEDOUT;
        }
        case L_FUTEX_WAKE:
            return 0;
        default:
            return -ENOSYS;
    }
}

static i64 l_getrandom(u64 buf, u64 len) {
    if (!uok(buf, len, true)) return -EFAULT;
    u8* out = (u8*)buf;
    for (u64 i = 0; i < len; i += 8) {
        u64 r = weak_random();
        for (u64 j = 0; j < 8 && i + j < len; j++) out[i + j] = (u8)(r >> (j * 8));
    }
    return (i64)len;
}

static i64 l_brk(u64 addr) {
    process_t* p = me();
    if (addr <= p->brk_base) return (i64)p->brk;       // brk(0): where is it?
    if (addr > p->brk) {
        if (sys_sbrk((i64)(addr - p->brk)) < 0) return (i64)p->brk;
    } else {
        u64 keep = PAGE_UP(addr), end = PAGE_UP(p->brk);
        if (end > keep) mm_munmap(keep, end - keep);
        p->brk = addr;
    }
    return (i64)p->brk;
}

// ---------------------------------------------------------------------------
// Signals
// ---------------------------------------------------------------------------

#define UNBLOCKABLE ((1u << SIGKILL) | (1u << SIGSTOP))

static i64 l_rt_sigaction(u64 sig, u64 uact, u64 uold, u64 size) {
    if (size != 8) return -EINVAL;
    if (sig == 0 || sig >= NSIG) return -EINVAL;
    if (uact && (sig == SIGKILL || sig == SIGSTOP)) return -EINVAL;
    if (uact && !uok(uact, sizeof(l_sigaction_t), false)) return -EFAULT;
    if (uold && !uok(uold, sizeof(l_sigaction_t), true)) return -EFAULT;
    process_t* p = me();
    u32 bit = 1u << sig;

    l_sigaction_t act;
    if (uact) memcpy(&act, (const void*)uact, sizeof(act));   // act and old may alias
    if (uold) {
        l_sigaction_t* o = (l_sigaction_t*)uold;
        o->handler = p->sig_handler[sig];
        o->flags = p->sig_flags[sig];
        o->restorer = (p->sig_linux & bit) ? p->sig_restorers[sig] : 0;
        o->mask = l_mask_to_sigset(p->sig_actmask[sig]);
    }
    if (uact) {
        p->sig_handler[sig] = act.handler;
        p->sig_flags[sig] = (u32)act.flags;
        p->sig_actmask[sig] = l_sigset_to_mask(act.mask) & ~UNBLOCKABLE;
        p->sig_restorers[sig] = act.restorer;
        if (act.handler > SIG_IGN) p->sig_linux |= bit;
        else p->sig_linux &= ~bit;
        // Setting a signal to ignored discards one already pending (POSIX).
        if (act.handler == SIG_IGN) p->sig_pending &= ~bit;
    }
    return 0;
}

static i64 l_rt_sigprocmask(u64 how, u64 uset, u64 uold, u64 size) {
    if (size != 8) return -EINVAL;
    if (uset && !uok(uset, 8, false)) return -EFAULT;
    if (uold && !uok(uold, 8, true)) return -EFAULT;
    process_t* p = me();
    u32 old = p->sig_mask;
    if (uset) {
        u32 s = l_sigset_to_mask(*(const u64*)uset) & ~UNBLOCKABLE;
        switch (how) {
            case SIG_BLOCK:   p->sig_mask |= s; break;
            case SIG_UNBLOCK: p->sig_mask &= ~s; break;
            case SIG_SETMASK: p->sig_mask = s; break;
            default: return -EINVAL;
        }
    }
    if (uold) *(u64*)uold = l_mask_to_sigset(old);
    return 0;
}

static i64 l_rt_sigpending(u64 uset, u64 size) {
    if (size != 8) return -EINVAL;
    if (!uok(uset, 8, true)) return -EFAULT;
    process_t* p = me();
    *(u64*)uset = l_mask_to_sigset(p->sig_pending & p->sig_mask);
    return 0;
}

// Wait until a signal arrives (pause, sigsuspend).
static i64 wait_for_signal(void) {
    static int nobody;
    while (!signal_pending(me())) sched_block_on(&nobody);
    return -EINTR;
}

static i64 l_rt_sigsuspend(u64 uset, u64 size) {
    if (!uset) return -EFAULT;
    i64 r = swap_mask(uset, size);
    if (r < 0) return r;
    return wait_for_signal();
}

static i64 l_sigaltstack(u64 uss, u64 uold) {
    (void)uss;                       // handlers always run on the normal stack
    if (uold) {
        if (!uok(uold, sizeof(l_stack_t), true)) return -EFAULT;
        l_stack_t* o = (l_stack_t*)uold;
        memset(o, 0, sizeof(*o));
        o->ss_flags = L_SS_DISABLE;
    }
    return 0;
}

void pm_sigreturn(registers_t* regs);

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------

static void log_unknown(u64 nr) {
    static u8 seen[512 / 8];
    if (nr < 512) {
        if (seen[nr / 8] & (1 << (nr % 8))) return;
        seen[nr / 8] |= (u8)(1 << (nr % 8));
    }
    kserialoutf("primemeridian: %s (pid %u) called unimplemented syscall %lu\n",
                me()->name, (u32)me()->pid, nr);
}

static void pm_dispatch(registers_t* regs) {
    u64 nr = regs->rax;
    u64 a = regs->rdi, b = regs->rsi, c = regs->rdx, d = regs->r10, e = regs->r8, f = regs->r9;
    process_t* p = me();
    p->syscall_nr = (i64)nr;                          // in case it must be restarted
    i64 r;

    switch (nr) {
        // files
        case LNX_read:        r = sys_read(a, b, c); break;
        case LNX_write:       r = sys_write(a, b, c); break;
        case LNX_open:        r = sys_open(a, b, c); break;
        case LNX_openat: {
            char kpath[VFS_PATH_MAX];
            r = path_at((i64)a, b, kpath);
            if (r == 0) r = open_kpath(kpath, c);
            break;
        }
        case LNX_creat:       r = sys_open(a, O_CREAT | O_WRONLY | O_TRUNC, b); break;
        case LNX_close:       r = fd_close(p, (i32)a); break;
        case LNX_stat:
        case LNX_lstat: {
            char kpath[VFS_PATH_MAX];
            r = copy_path(kpath, a);
            if (r == 0) r = stat_kpath(kpath, b);
            break;
        }
        case LNX_fstat:       r = l_fstat(a, b); break;
        case LNX_newfstatat:  r = l_newfstatat((i64)a, b, c, d); break;
        case LNX_statfs:      r = l_statfs(a, b); break;
        case LNX_fstatfs:     r = l_fstatfs(a, b); break;
        case LNX_lseek:       r = sys_lseek(a, (i64)b, c); break;
        case LNX_pread64:     r = l_pread(a, b, c, (i64)d, false); break;
        case LNX_pwrite64:    r = l_pread(a, b, c, (i64)d, true); break;
        case LNX_readv:       r = l_rw_vec(a, b, c, false); break;
        case LNX_writev:      r = l_rw_vec(a, b, c, true); break;
        case LNX_access:      r = exists_at(L_AT_FDCWD, a); break;
        case LNX_faccessat:
        case LNX_faccessat2:  r = exists_at((i64)a, b); break;
        case LNX_pipe:        r = sys_pipe(a); break;
        case LNX_pipe2:       r = l_pipe2(a, b); break;
        case LNX_dup:         r = sys_dup(a); break;
        case LNX_dup2:        r = sys_dup2(a, b); break;
        case LNX_dup3:        r = l_dup3(a, b, c); break;
        case LNX_fcntl:       r = l_fcntl(a, b, c); break;
        case LNX_sendfile:    r = l_sendfile(a, b, c, d); break;
        case LNX_ioctl:       r = l_ioctl(a, b, c); break;
        case LNX_getdents64:  r = l_getdents64(a, b, c); break;
        case LNX_getcwd:      r = sys_getcwd(a, b); if (r >= 0) r++; break;   // Linux counts the NUL
        case LNX_chdir:       r = sys_chdir(a); break;
        case LNX_fchdir:      r = l_fchdir(a); break;
        case LNX_mkdir:       r = sys_path_op(a, SYS_MKDIR); break;
        case LNX_rmdir:       r = sys_path_op(a, SYS_RMDIR); break;
        case LNX_unlink:      r = sys_path_op(a, SYS_UNLINK); break;
        case LNX_mkdirat: {
            char kpath[VFS_PATH_MAX];
            vnode_t n;
            r = path_at((i64)a, b, kpath);
            if (r == 0) r = vfs_create(kpath, VN_DIR, &n);
            break;
        }
        case LNX_unlinkat: {
            char kpath[VFS_PATH_MAX];
            r = path_at((i64)a, b, kpath);
            if (r == 0) r = vfs_unlink(kpath, (c & L_AT_REMOVEDIR) != 0);
            break;
        }
        case LNX_rename:      r = sys_rename(a, b); break;
        case LNX_renameat:
        case LNX_renameat2: {
            char from[VFS_PATH_MAX], to[VFS_PATH_MAX];
            if (nr == LNX_renameat2 && e & ~1ULL) { r = -EINVAL; break; }
            r = path_at((i64)a, b, from);
            if (r == 0) r = path_at((i64)c, d, to);
            if (r == 0 && nr == LNX_renameat2 && (e & 1)) {           // RENAME_NOREPLACE
                vnode_t n;
                if (vfs_lookup(to, &n) == 0) { r = -EEXIST; break; }
            }
            if (r == 0) r = vfs_rename(from, to);
            break;
        }
        case LNX_truncate:    r = l_truncate(a, (i64)b); break;
        case LNX_ftruncate:   r = sys_ftruncate(a, (i64)b); break;
        case LNX_readlink:    r = l_readlinkat(L_AT_FDCWD, a); break;
        case LNX_readlinkat:  r = l_readlinkat((i64)a, b); break;
        case LNX_link: case LNX_linkat:
        case LNX_symlink: case LNX_symlinkat:
        case LNX_mknod:       r = -EPERM; break;        // FAT32 has no links or nodes
        case LNX_chmod: case LNX_chown: case LNX_lchown:
        case LNX_utime: case LNX_utimes:
                              r = exists_at(L_AT_FDCWD, a); break;
        case LNX_fchmodat: case LNX_fchownat:
                              r = exists_at((i64)a, b); break;
        case LNX_utimensat:   r = b ? exists_at((i64)a, b) : (fd_get(p, (i32)a) ? 0 : -EBADF); break;
        case LNX_fchmod: case LNX_fchown:
        case LNX_fsync: case LNX_fdatasync:
        case LNX_flock: case LNX_fadvise64:
                              r = fd_get(p, (i32)a) ? 0 : -EBADF; break;
        case LNX_sync:        r = 0; break;
        case LNX_umask:       r = p->umask; p->umask = (u32)a & 0777; break;

        // memory
        case LNX_brk:         r = l_brk(a); break;
        case LNX_mmap:        r = mm_mmap(a, b, c, d, (i64)e, f); break;
        case LNX_munmap:      r = mm_munmap(a, b); break;
        case LNX_mprotect:    r = mm_mprotect(a, b, c); break;
        case LNX_mremap:      r = -ENOMEM; break;       // callers fall back to mmap+copy
        case LNX_madvise: case LNX_msync:
        case LNX_mlock: case LNX_munlock:
                              r = 0; break;

        // poll, select, time
        case LNX_poll:        r = l_poll(a, b, (i64)(i32)c); break;
        case LNX_ppoll:       r = l_ppoll(a, b, c, d, e); break;
        case LNX_select:      r = l_select(a, b, c, d, e); break;
        case LNX_pselect6:    r = l_pselect6(a, b, c, d, e, f); break;
        case LNX_nanosleep:   r = l_nanosleep(a, b); break;
        case LNX_clock_nanosleep: r = l_clock_nanosleep(a, b, c, d); break;
        case LNX_clock_gettime:   r = l_clock_gettime(a, b); break;
        case LNX_clock_getres:
            if (b) {
                if (!uok(b, sizeof(l_timespec_t), true)) { r = -EFAULT; break; }
                ((l_timespec_t*)b)->tv_sec = 0;
                ((l_timespec_t*)b)->tv_nsec = 4000000;
            }
            r = 0;
            break;
        case LNX_gettimeofday: r = l_gettimeofday(a, b); break;
        case LNX_time:
            r = (i64)rtc_now();
            if (a) { if (uok(a, 8, true)) *(i64*)a = r; else r = -EFAULT; }
            break;
        case LNX_times:       r = l_times(a); break;
        case LNX_alarm:       r = l_alarm(a); break;
        case LNX_setitimer:   r = l_setitimer(a, b, c); break;
        case LNX_getitimer:   r = l_setitimer(a, 0, b); break;

        // processes
        case LNX_getpid: case LNX_gettid:
        case LNX_set_tid_address:
                              r = p->pid; break;
        case LNX_getppid:     r = p->ppid; break;
        case LNX_getuid: case LNX_geteuid: case LNX_getgid: case LNX_getegid:
                              r = 0; break;             // everyone is root
        case LNX_setuid: case LNX_setgid: case LNX_setreuid: case LNX_setregid:
        case LNX_setresuid: case LNX_setresgid: case LNX_setgroups:
        case LNX_setfsuid: case LNX_setfsgid:
                              r = 0; break;
        case LNX_getresuid: case LNX_getresgid:
            if (!uok(a, 4, true) || !uok(b, 4, true) || !uok(c, 4, true)) { r = -EFAULT; break; }
            *(u32*)a = *(u32*)b = *(u32*)c = 0;
            r = 0;
            break;
        case LNX_getgroups:   r = 0; break;
        case LNX_setpgid:     r = sys_setpgid((i64)a, (i64)b); break;
        case LNX_getpgid:     r = sys_getpgid((i64)a); break;
        case LNX_getpgrp:     r = p->pgid; break;
        case LNX_setsid:      r = l_setsid(); break;
        case LNX_getsid:      r = l_getsid((i64)a); break;
        case LNX_fork:
        case LNX_vfork: {
            process_t* child = proc_fork(regs);
            r = child ? child->pid : -ENOMEM;
            break;
        }
        case LNX_clone:       r = l_clone(regs, a, b, c, d, e); break;
        case LNX_execve:      r = sys_execve(regs, a, b, c); break;
        case LNX_exit:
        case LNX_exit_group:  proc_exit((i64)a);         // does not return
        case LNX_wait4:       r = l_wait4((i64)a, b, c, d); break;
        case LNX_kill:        r = l_kill((i64)a, b); break;
        case LNX_tkill:       r = l_kill((i64)a, b); break;
        case LNX_tgkill:      r = l_kill((i64)b, c); break;
        case LNX_uname:       r = l_uname(a); break;
        case LNX_getrlimit:   r = l_prlimit(a, 0, b); break;
        case LNX_setrlimit:   r = 0; break;
        case LNX_prlimit64:   r = l_prlimit(b, c, d); break;
        case LNX_getrusage:
            if (!uok(b, sizeof(l_rusage_t), true)) { r = -EFAULT; break; }
            memset((void*)b, 0, sizeof(l_rusage_t));
            r = 0;
            break;
        case LNX_sysinfo:     r = l_sysinfo(a); break;
        case LNX_prctl:       r = l_prctl(a, b); break;
        case LNX_arch_prctl:  r = l_arch_prctl(a, b); break;
        case LNX_futex:       r = l_futex(a, b, c, d); break;
        case LNX_set_robust_list: r = 0; break;
        case LNX_getrandom:   r = l_getrandom(a, b); break;
        case LNX_sched_yield: schedule(); r = 0; break;
        case LNX_sched_getaffinity:
            if (b < 8 || !uok(c, 8, true)) { r = -EINVAL; break; }
            *(u64*)c = 1;                              // one CPU
            r = 8;
            break;
        case LNX_sched_getscheduler: r = 0; break;     // SCHED_OTHER
        case LNX_sched_getparam:
            if (!uok(b, 4, true)) { r = -EFAULT; break; }
            *(i32*)b = 0;
            r = 0;
            break;
        case LNX_getpriority: r = 20; break;           // nice 0, in the syscall's 40-n form
        case LNX_setpriority: r = 0; break;
        case LNX_personality: r = 0; break;            // PER_LINUX, always
        case LNX_socket:      r = -L_EAFNOSUPPORT; break; // no networking yet

        // signals
        case LNX_rt_sigaction:   r = l_rt_sigaction(a, b, c, d); break;
        case LNX_rt_sigprocmask: r = l_rt_sigprocmask(a, b, c, d); break;
        case LNX_rt_sigpending:  r = l_rt_sigpending(a, b); break;
        case LNX_rt_sigsuspend:  r = l_rt_sigsuspend(a, b); break;
        case LNX_pause:          r = wait_for_signal(); break;
        case LNX_sigaltstack:    r = l_sigaltstack(a, b); break;
        case LNX_rt_sigreturn:
            // The restored frame may hold -EINTR from the call the handler
            // interrupted; that result stands, it must not be "restarted".
            p->syscall_nr = -1;
            pm_sigreturn(regs);
            return;

        default:
            log_unknown(nr);
            r = -ENOSYS;
            break;
    }
    regs->rax = (u64)r;
}

// Entered from pm_syscall_entry (entry.s) with the caller's full frame.
void pm_syscall(registers_t* regs) {
    pm_dispatch(regs);
    signal_deliver_from(regs, true);
    // A call that swapped in a temporary signal mask (sigsuspend, ppoll,
    // pselect6) is over: put the caller's back, unless a handler frame
    // already took it to restore on sigreturn.
    process_t* p = me();
    if (p->sig_mask_saved) {
        p->sig_mask = p->sig_saved_mask;
        p->sig_mask_saved = false;
    }
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

extern char pm_syscall_entry[];
extern u64 pm_kernel_rsp;

void pm_set_kernel_stack(u64 top) {
    pm_kernel_rsp = top;
}

void pm_init(void) {
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);
    // syscall loads CS = STAR[47:32] and SS = that + 8: our kernel code and
    // data. (sysret would use STAR[63:48] + 16 / + 8 -- the user selectors --
    // but we leave through iretq.)
    wrmsr(MSR_STAR, (u64)(GDT_USER_DS - 8) << 48 | (u64)GDT_KERNEL_CS << 32);
    wrmsr(MSR_LSTAR, (u64)pm_syscall_entry);
    // Enter with interrupts off (like our int 0x80 gate), and with TF, DF,
    // AC, NT and IOPL cleared, as Linux does.
    wrmsr(MSR_FMASK, 0x47700);
}
