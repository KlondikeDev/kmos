// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// The syscall table.
//
// Ring 3 may hand us any pointer at all. Faulting on a bad one would be a
// kernel-mode page fault, i.e. a panic, so every buffer is proved to be
// mapped user-accessible -- with the right permissions -- before it is
// touched (EFAULT otherwise). Syscalls run in the caller's address space,
// so once checked, a user pointer can be used directly.

#include <types.h>
#include <wifi_control.h>
#include <wifi_ioctl.h>
#include <build.h>
#include <syscall.h>
#include <usermode.h>
#include <elf.h>
#include <proc.h>
#include <file.h>
#include <vfs.h>
#include <tty.h>
#include <devfs.h>
#include <disk_ioctl.h>
#include <signal.h>
#include <errno.h>
#include <pit.h>
#include <rtc.h>
#include <memory.h>
#include <disk.h>
#include <inet.h>
#include <socket.h>
#include <memcpy.h>
#include <memset.h>
#include <klog.h>
#include <klibc/string.h>
#include <mman.h>
#include <ksys.h>

#define PAGE_MASK (~(USER_PAGE_SIZE - 1))

static process_t* me(void) {
    return proc_current();
}

i32 copy_path(char* dst, u64 src) {
    if (!src) return -EFAULT;
    if (!copy_string_from_user(dst, src, VFS_PATH_MAX)) {
        // Either unmapped or too long; tell them apart for a better errno.
        return user_range_ok(src, 1, false) ? -ENAMETOOLONG : -EFAULT;
    }
    return 0;
}

// Gather a NULL-terminated array of user strings into args.
static i32 copy_strv(u64 vec, u32* count, const char** out, char* pool, u64* used) {
    *count = 0;
    if (!vec) return 0;
    for (;;) {
        if (!user_range_ok(vec + *count * 8, 8, false)) return -EFAULT;
        u64 s = *(const u64*)(vec + *count * 8);
        if (!s) return 0;
        if (*count == USER_MAX_ARGS) return -E2BIG;
        if (*used >= USER_MAX_ARGBYTES) return -E2BIG;
        if (!copy_string_from_user(pool + *used, s, USER_MAX_ARGBYTES - *used)) {
            return user_range_ok(s, 1, false) ? -E2BIG : -EFAULT;
        }
        out[(*count)++] = pool + *used;
        *used += strlen(pool + *used) + 1;
    }
}

static i32 copy_args(u64 argv, u64 envp, user_args_t* a) {
    u64 used = 0;
    i32 r = copy_strv(argv, &a->argc, a->argv, a->strings, &used);
    if (r < 0) return r;
    return copy_strv(envp, &a->envc, a->envp, a->strings, &used);
}

// ---- files ----

i64 sys_read(u64 fd, u64 buf, u64 len) {
    file_t* f = fd_get(me(), (i32)fd);
    if (!f) return -EBADF;
    if (!user_range_ok(buf, len, true)) return -EFAULT;
    return file_read(f, (void*)buf, len);
}

i64 sys_write(u64 fd, u64 buf, u64 len) {
    file_t* f = fd_get(me(), (i32)fd);
    if (!f) return -EBADF;
    if (!user_range_ok(buf, len, false)) return -EFAULT;
    return file_write(f, (const void*)buf, len);
}

i64 sys_open(u64 path, u64 flags, u64 mode) {
    char kpath[VFS_PATH_MAX];
    i32 r = copy_path(kpath, path);
    if (r < 0) return r;
    file_t* f;
    r = file_open_mode(kpath, (u32)flags, (u32)mode & 07777, &f);
    if (r < 0) return r;
    i32 fd = fd_install(me(), f, 0);
    if (fd < 0) {
        file_unref(f);
        return fd;
    }
    if (flags & O_CLOEXEC) me()->fd_cloexec |= 1u << fd;
    return fd;
}

i64 sys_lseek(u64 fd, i64 off, u64 whence) {
    file_t* f = fd_get(me(), (i32)fd);
    if (!f) return -EBADF;
    if (!f->node || f->node->type == VN_SOCK || f->node->type == VN_FIFO || (f->node->type == VN_CHR && !devfs_is_fb(f->node))) return -ESPIPE;
    i64 base;
    switch (whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = (i64)f->pos; break;
        case SEEK_END: base = (i64)f->node->size; break;
        default: return -EINVAL;
    }
    if (base + off < 0) return -EINVAL;
    f->pos = (u64)(base + off);
    return (i64)f->pos;
}

void fill_stat(const vnode_t* n, kstat_t* st) {
    memset(st, 0, sizeof(*st));
    st->st_dev = n->mnt ? n->mnt->index : 0;
    st->st_ino = n->id;
    st->st_nlink = 1;
    st->st_size = n->size;
    switch (n->type) {
        case VN_DIR:  st->st_mode = S_IFDIR | 0755; break;
        case VN_BLK:  st->st_mode = S_IFBLK | 0600; break;
        case VN_CHR:  st->st_mode = S_IFCHR | 0666; break;
        case VN_FIFO: st->st_mode = S_IFIFO | 0600; break;
        case VN_LNK:  st->st_mode = S_IFLNK | 0777; break;
        case VN_SOCK: st->st_mode = S_IFSOCK | 0777; break;
        default:      st->st_mode = S_IFREG | 0644; break;
    }
    // The permissions the checks use: the filesystem's own, or stand-ins.
    u32 mode, uid, gid;
    vfs_attrs(n, &mode, &uid, &gid);
    st->st_mode = (st->st_mode & S_IFMT) | mode;
    if (n->nlink || n->mode) st->st_nlink = n->nlink;
}

static i64 sys_fstat(u64 fd, u64 out) {
    file_t* f = fd_get(me(), (i32)fd);
    if (!f) return -EBADF;
    if (!user_range_ok(out, sizeof(kstat_t), true)) return -EFAULT;
    if(!f->node){
        if(!file_is_inet(f)&&!file_is_socket(f))return -ENOSYS;
        kstat_t st;memset(&st,0,sizeof(st));st.st_mode=S_IFSOCK|0777;st.st_nlink=1;
        memcpy((void*)out,&st,sizeof(st));return 0;
    }
    vfs_getattr(f->node);
    fill_stat(f->node, (kstat_t*)out);
    return 0;
}

static i64 sys_stat(u64 path, u64 out) {
    char kpath[VFS_PATH_MAX];
    i32 r = copy_path(kpath, path);
    if (r < 0) return r;
    if (!user_range_ok(out, sizeof(kstat_t), true)) return -EFAULT;
    vnode_t n;
    r = vfs_lookup(kpath, &n);
    if (r < 0) return r;
    fill_stat(&n, (kstat_t*)out);
    return 0;
}

i64 sys_dup2(u64 oldfd, u64 newfd) {
    process_t* p = me();
    file_t* f = fd_get(p, (i32)oldfd);
    if (!f) return -EBADF;
    if (newfd >= MAX_FDS) return -EBADF;
    if (oldfd == newfd) return (i64)newfd;
    if (p->fds[newfd]) fd_close(p, (i32)newfd);
    file_ref(f);
    p->fds[newfd] = f;
    p->fd_cloexec &= ~(1u << newfd);
    return (i64)newfd;
}

i64 sys_dup(u64 fd) {
    file_t* f = fd_get(me(), (i32)fd);
    if (!f) return -EBADF;
    i32 nfd = fd_install(me(), f, 0);
    if (nfd >= 0) file_ref(f);
    return nfd;
}

i64 sys_pipe(u64 out) {
    if (!user_range_ok(out, 2 * sizeof(i32), true)) return -EFAULT;
    file_t *r, *w;
    i32 e = pipe_create(&r, &w);
    if (e < 0) return e;
    i32 rfd = fd_install(me(), r, 0);
    if (rfd < 0) { file_unref(r); file_unref(w); return rfd; }
    i32 wfd = fd_install(me(), w, 0);
    if (wfd < 0) { fd_close(me(), rfd); file_unref(w); return wfd; }
    ((i32*)out)[0] = rfd;
    ((i32*)out)[1] = wfd;
    return 0;
}

static i64 sys_readdir(u64 fd, u64 out) {
    file_t* f = fd_get(me(), (i32)fd);
    if (!f) return -EBADF;
    if (!f->node || f->node->type != VN_DIR || !f->path) return -ENOTDIR;
    if (!user_range_ok(out, sizeof(kdirent_t), true)) return -EFAULT;

    kdirent_t d;
    memset(&d, 0, sizeof(d));
    vnode_t n;
    i32 r = vfs_readdir_path(f->path, (u32)f->pos, d.d_name, &n);
    if (r == 1) {
        f->pos++;
        d.d_ino = n.id;
        d.d_size = n.size;
        d.d_type = n.type;
        memcpy((void*)out, &d, sizeof(d));
    }
    return r;
}

i64 sys_path_op(u64 path, u64 op) {
    char kpath[VFS_PATH_MAX];
    i32 r = copy_path(kpath, path);
    if (r < 0) return r;
    vnode_t n;
    switch (op) {
        case SYS_MKDIR:  return vfs_create(kpath, VN_DIR, &n);
        case SYS_UNLINK: return vfs_unlink(kpath, false);
        case SYS_RMDIR:  return vfs_unlink(kpath, true);
        default:         return -ENOSYS;
    }
}

i64 sys_ftruncate(u64 fd, i64 len) {
    file_t* f = fd_get(me(), (i32)fd);
    if (!f) return -EBADF;
    if (len < 0) return -EINVAL;
    if ((f->flags & O_ACCMODE) == O_RDONLY) return -EBADF;
    return vfs_truncate(f->node, (u64)len);
}

i64 sys_chdir(u64 path) {
    char kpath[VFS_PATH_MAX];
    i32 r = copy_path(kpath, path);
    if (r < 0) return r;
    return vfs_chdir(kpath);
}

i64 sys_getcwd(u64 buf, u64 len) {
    const char* cwd = me()->cwd;
    u64 n = strlen(cwd) + 1;
    if (n > len) return -ERANGE;
    if (!user_range_ok(buf, n, true)) return -EFAULT;
    memcpy((void*)buf, cwd, n);
    return (i64)(n - 1);
}

static i64 sys_ioctl(u64 fd, u64 req, u64 arg) {
    file_t* f = fd_get(me(), (i32)fd);
    if (!f) return -EBADF;
    if(devfs_is_wifi(f->node)) {
        u32 size=wifi_control_size((u32)req);
        if(size && !user_range_ok(arg,size,req==KWIFI_RESULT))return -EFAULT;
        return wifi_control_ioctl((u32)req,(void*)arg);
    }
    if(devfs_is_disk(f->node)) {
        u32 size=devfs_disk_ioctl_size((u32)req);
        if(size && !user_range_ok(arg,size,true)) return -EFAULT;
        if(req==KDISK_RESCAN && !(f->flags&O_ACCMODE)) return -EBADF;
        return devfs_disk_ioctl(f->node,(u32)req,(void*)arg);
    }
    tty_t* t = file_tty(f);
    if (!t) return -ENOTTY;
    switch (req) {
        case TIOCGMODE: return tty_get_mode(t);
        case TIOCSMODE: tty_set_mode(t, (u32)arg); return 0;
        default:        return -EINVAL;
    }
}

static i64 sys_tcsetpgrp(u64 fd, u64 pgid) {
    file_t* f = fd_get(me(), (i32)fd);
    if (!f) return -EBADF;
    tty_t* t = file_tty(f);
    if (!t) return -ENOTTY;
    t->fg_pgid = (i32)pgid;
    return 0;
}

static i64 sys_tcgetpgrp(u64 fd) {
    file_t* f = fd_get(me(), (i32)fd);
    if (!f) return -EBADF;
    tty_t* t = file_tty(f);
    if (!t) return -ENOTTY;
    return t->fg_pgid;
}

// ---- processes ----

static i64 sys_spawn(u64 path, u64 argv, u64 envp) {
    char kpath[VFS_PATH_MAX];
    i32 r = copy_path(kpath, path);
    if (r < 0) return r;
    user_args_t* a = kmalloc(sizeof(user_args_t));
    if (!a) return -ENOMEM;
    r = copy_args(argv, envp, a);
    if (r == 0) r = usermode_spawn(kpath, a, me());
    kfree(a);
    return r;
}

i64 sys_execve(registers_t* regs, u64 path, u64 argv, u64 envp) {
    char kpath[VFS_PATH_MAX];
    i32 r = copy_path(kpath, path);
    if (r < 0) return r;
    user_args_t* a = kmalloc(sizeof(user_args_t));
    if (!a) return -ENOMEM;
    // The strings must be copied out before the old image is torn down.
    r = copy_args(argv, envp, a);
    if (r == 0) r = usermode_exec(kpath, a, regs);
    kfree(a);
    return r;
}

i64 sys_waitpid(u64 pid, u64 status_ptr, u64 options) {
    if (status_ptr && !user_range_ok(status_ptr, sizeof(i32), true)) return -EFAULT;
    i64 status = 0;
    i32 r = proc_wait((i32)pid, &status, (u32)options);
    if (r > 0 && status_ptr) *(i32*)status_ptr = (i32)status;
    return r;
}

i64 sys_sbrk(i64 increment) {
    process_t* p = me();
    u64 old = p->brk;
    if (increment == 0) return (i64)old;
    if (increment < 0) {
        // Shrinking just moves the break; pages stay mapped until exit.
        if ((u64)-increment > old - p->brk_base) return -EINVAL;
        p->brk = old + (u64)increment;
        return (i64)old;
    }
    u64 new_brk = old + (u64)increment;
    if (new_brk < old || new_brk > USER_LOAD_LIMIT) return -ENOMEM;
    if (mm_overlaps(old & PAGE_MASK, new_brk)) return -ENOMEM;

    u64 first = (old + USER_PAGE_SIZE - 1) & PAGE_MASK;
    for (u64 page = first; page < new_brk; page += USER_PAGE_SIZE) {
        if (!user_map(p, page, PF_R | PF_W)) return -ENOMEM;
    }
    p->brk = new_brk;
    return (i64)old;
}

static i64 sys_sleep(u64 ms) {
    if (ms == 0) return 0;
    u64 n = (ms * 250 + 999) / 1000;                         // PIT runs at 250 Hz
    if (n > 0x7FFFFFFFULL) n = 0x7FFFFFFFULL;
    sched_sleep((u32)n);
    return signal_pending(me()) ? -EINTR : 0;
}

static i64 sys_procinfo(u64 index, u64 out) {
    if (!user_range_ok(out, sizeof(proc_info_t), true)) return -EFAULT;
    proc_info_t info;
    if (!proc_info((u32)index, &info)) return 0;
    memcpy((void*)out, &info, sizeof(info));
    return 1;
}

i64 sys_kill(i64 pid, u64 sig) {
    if (sig >= NSIG) return -EINVAL;
    if (pid < -1) {
        i32 n = signal_send_group_as(me(), (i32)-pid, (i32)sig);
        return n < 0 ? n : 0;
    }
    if (pid <= 0) return -EINVAL;                    // -1 / 0: not supported yet
    process_t* p = proc_by_pid((i32)pid);
    if (!p || p->pid == 0 || p->state == PROC_ZOMBIE) return -ESRCH;
    if (!signal_may(me(), p, (i32)sig)) return -EPERM;
    if (sig) signal_send(p, (i32)sig);
    return 0;
}

static i64 sys_sigaction(u64 sig, u64 handler, u64 restorer) {
    if (sig == 0 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP) return -EINVAL;
    if (handler > SIG_IGN && !user_range_ok(handler, 1, false)) return -EFAULT;
    process_t* p = me();
    u64 old = p->sig_handler[sig];
    p->sig_handler[sig] = handler;
    if (restorer) p->sig_restorer = restorer;
    // Setting a signal to ignored discards one already pending (POSIX).
    if (handler == SIG_IGN) p->sig_pending &= ~(1u << sig);
    return (i64)old;
}

static i64 sys_sigprocmask(u64 how, u64 set) {
    process_t* p = me();
    u32 old = p->sig_mask;
    u32 s = (u32)set & ~((1u << SIGKILL) | (1u << SIGSTOP));
    switch (how) {
        case SIG_BLOCK:   p->sig_mask |= s; break;
        case SIG_UNBLOCK: p->sig_mask &= ~s; break;
        case SIG_SETMASK: p->sig_mask = s; break;
        default: return -EINVAL;
    }
    return old;
}

i64 sys_setpgid(i64 pid, i64 pgid) {
    process_t* p = pid == 0 ? me() : proc_by_pid((i32)pid);
    if (!p || p->state == PROC_ZOMBIE) return -ESRCH;
    // Only yourself or your own children.
    if (p != me() && p->ppid != me()->pid) return -ESRCH;
    if (pgid < 0) return -EINVAL;
    p->pgid = pgid == 0 ? p->pid : (i32)pgid;
    return 0;
}

i64 sys_getpgid(i64 pid) {
    process_t* p = pid == 0 ? me() : proc_by_pid((i32)pid);
    if (!p) return -ESRCH;
    return p->pgid;
}

static void copy_str(char* dst, const char* src, u32 cap) {
    u32 i = 0;
    for (; src[i] && i < cap - 1; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static i64 sys_uname(u64 out) {
    if (!user_range_ok(out, sizeof(kutsname_t), true)) return -EFAULT;
    kutsname_t u;
    memset(&u, 0, sizeof(u));
    copy_str(u.sysname, "Kunix", sizeof(u.sysname));
    copy_str(u.release, KUNIX_RELEASE, sizeof(u.release));
    copy_str(u.version, KUNIX_BUILD_DESCRIPTION, sizeof(u.version));
    copy_str(u.machine, "x86_64", sizeof(u.machine));
    memcpy((void*)out, &u, sizeof(u));
    return 0;
}

static i64 sys_meminfo(u64 out) {
    if (!user_range_ok(out, sizeof(kmeminfo_t), true)) return -EFAULT;
    kmeminfo_t m;
    m.total_pages = pmm_total_pages();
    m.free_pages = pmm_count_free();
    uint64_t used, free_bytes;
    heap_stats(&used, &free_bytes);
    m.heap_used = used;
    m.heap_free = free_bytes;
    memcpy((void*)out, &m, sizeof(m));
    return 0;
}

i64 sys_rename(u64 oldpath, u64 newpath) {
    char from[VFS_PATH_MAX], to[VFS_PATH_MAX];
    i32 r = copy_path(from, oldpath);
    if (r < 0) return r;
    r = copy_path(to, newpath);
    if (r < 0) return r;
    return vfs_rename(from, to);
}

bool net_native_call(u64,u64,u64,u64,u64,u64,u64,i64*);

// ---- dispatch ----

void syscall_dispatch(registers_t* regs) {
    u64 a = regs->rdi, b = regs->rsi, c = regs->rdx;
    i64 r;
    me()->syscall_nr = (i64)regs->rax;          // in case it must be restarted

    switch (regs->rax) {
        case SYS_EXIT:        proc_exit((i64)a);                 // does not return
        case SYS_WRITE:       r = sys_write(a, b, c); break;
        case SYS_GETCPL:      r = (i64)(regs->cs & 3); break;    // CPL = low bits of CS
        case SYS_READ:        r = sys_read(a, b, c); break;
        case SYS_OPEN:        r = sys_open(a, b, c); break;
        case SYS_CLOSE:       r = fd_close(me(), (i32)a); break;
        case SYS_SPAWN:       r = sys_spawn(a, b, c); break;
        case SYS_WAITPID:     r = sys_waitpid(a, b, c); break;
        case SYS_GETPID:      r = me()->pid; break;
        case SYS_SBRK:        r = sys_sbrk((i64)a); break;
        case SYS_YIELD:       schedule(); r = 0; break;
        case SYS_SLEEP:       r = sys_sleep(a); break;
        case SYS_READDIR:     r = sys_readdir(a, b); break;
        case SYS_CHDIR:       r = sys_chdir(a); break;
        case SYS_GETCWD:      r = sys_getcwd(a, b); break;
        case SYS_PROCINFO:    r = sys_procinfo(a, b); break;
        case SYS_LSEEK:       r = sys_lseek(a, (i64)b, c); break;
        case SYS_FSTAT:       r = sys_fstat(a, b); break;
        case SYS_STAT:        r = sys_stat(a, b); break;
        case SYS_DUP:         r = sys_dup(a); break;
        case SYS_DUP2:        r = sys_dup2(a, b); break;
        case SYS_PIPE:        r = sys_pipe(a); break;
        case SYS_MKDIR:
        case SYS_UNLINK:
        case SYS_RMDIR:       r = sys_path_op(a, regs->rax); break;
        case SYS_KILL:        r = sys_kill((i64)a, b); break;
        case SYS_SIGACTION:   r = sys_sigaction(a, b, c); break;
        case SYS_SIGRETURN:
            // The restored frame may hold -EINTR from the call the handler
            // interrupted; that result stands, it must not be "restarted".
            me()->syscall_nr = -1;
            signal_return(regs);
            return;
        case SYS_GETPPID:     r = me()->ppid; break;
        case SYS_SETPGID:     r = sys_setpgid((i64)a, (i64)b); break;
        case SYS_GETPGID:     r = sys_getpgid((i64)a); break;
        case SYS_TCSETPGRP:   r = sys_tcsetpgrp(a, b); break;
        case SYS_TCGETPGRP:   r = sys_tcgetpgrp(a); break;
        case SYS_FORK: {
            process_t* child = proc_fork(regs);
            r = child ? child->pid : -ENOMEM;
            break;
        }
        case SYS_EXECVE:      r = sys_execve(regs, a, b, c); break;
        case SYS_UPTIME:      r = (i64)ticks * 4; break;         // 250 Hz
        case SYS_IOCTL:       r = sys_ioctl(a, b, c); break;
        case SYS_FTRUNCATE:   r = sys_ftruncate(a, (i64)b); break;
        case SYS_SIGPROCMASK: r = sys_sigprocmask(a, b); break;
        case SYS_UNAME:       r = sys_uname(a); break;
        case SYS_MEMINFO:     r = sys_meminfo(a); break;
        case SYS_TIME:        r = (i64)rtc_now(); break;
        case SYS_RENAME:      r = sys_rename(a, b); break;
        case SYS_MMAP:        r = mm_mmap(a, b, c, regs->r10, (i64)regs->r8, regs->r9); break;
        case SYS_MUNMAP:      r = mm_munmap(a, b); break;
        case SYS_MPROTECT:    r = mm_mprotect(a, b, c); break;
        case SYS_SYNC:        r = vfs_sync_all(); break;
        case SYS_FSYNC: {
            file_t* f = fd_get(me(), (i32)a);
            r = !f ? -EBADF : !f->node ? -EINVAL : vfs_sync(f->node);
            break;
        }
        default:
            if(!net_native_call(regs->rax,a,b,c,regs->r10,regs->r8,regs->r9,&r))r=-ENOSYS;
            break;
    }
    regs->rax = (u64)r;
    disk_cache_poll();
    net_poll();
}
