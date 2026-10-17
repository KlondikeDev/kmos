// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// procfs, mounted at /proc: the kernel's state as text files, in the
// formats Linux uses, so ps, top, free, mount and friends work unchanged.
//
//   /proc/<pid>/{stat,status,cmdline,comm}   one directory per process
//   /proc/self                               the reader's own directory
//   /proc/{meminfo,uptime,loadavg,stat,version,mounts,cpuinfo,filesystems}
//
// A file's contents are generated afresh on every read and then sliced by
// offset, so a reader that takes it in pieces sees consistent pieces only
// if nothing changed in between -- as on Linux, read it in one go.

#include <types.h>
#include <build.h>
#include <procfs.h>
#include <vfs.h>
#include <proc.h>
#include <tty.h>
#include <memory.h>
#include <memcpy.h>
#include <errno.h>
#include <pit.h>
#include <rtc.h>
#include <kprintf.h>
#include <klibc/string.h>
#include <file.h>
#include <klog.h>
#include <battery.h>
#include <disk.h>
#include <pagecache.h>
#include <inet.h>
#include <iwx200.h>

void cpuid_vendor(char* vendor);
void cpuid_brand(char* brand);

#define HZ 100                       // USER_HZ: what /proc's tick counts are in
#define TO_HZ(t) ((u64)(t) * HZ / 250)

// Node ids: the root is 1; global files are 2..; a process's directory is
// pid << 8 and its files pid << 8 | kind.
enum {
    P_ROOT = 1, P_SELF, P_MEMINFO, P_UPTIME, P_LOADAVG, P_STAT, P_VERSION,
    P_MOUNTS, P_CPUINFO, P_FILESYSTEMS, P_KMSG, P_BATTERY, P_CACHE, P_NETINFO, P_WIFI, P_GLOBAL_END,
};
enum { F_DIR = 0, F_STAT, F_STATUS, F_CMDLINE, F_COMM, F_FD, F_EXE, F_CWD, F_END };
// .../fd/N nodes (opening them is file_open's job: it shares descriptor N).
#define F_FD_ENTRY 0x80

static const char* const global_names[P_GLOBAL_END] = {
    [P_SELF] = "self", [P_MEMINFO] = "meminfo", [P_UPTIME] = "uptime",
    [P_LOADAVG] = "loadavg", [P_STAT] = "stat", [P_VERSION] = "version",
    [P_MOUNTS] = "mounts", [P_CPUINFO] = "cpuinfo", [P_FILESYSTEMS] = "filesystems",
    [P_KMSG] = "kmsg",
    [P_BATTERY] = "battery", [P_CACHE] = "cache", [P_NETINFO] = "netinfo", [P_WIFI] = "wifi",
};
static const char* const proc_file_names[F_END] = {
    [F_STAT] = "stat", [F_STATUS] = "status", [F_CMDLINE] = "cmdline", [F_COMM] = "comm",
    [F_FD] = "fd", [F_EXE] = "exe", [F_CWD] = "cwd",
};

static void make_node(u64 id, vnode_t* out) {
    out->id = id;
    out->aux = 0;
    out->size = 0;                   // generated: unknown until read, as on Linux
    out->mtime = rtc_now();
    u64 kind = id & 0xFF;
    bool dir = id == P_ROOT || id == P_SELF || (id >= 256 && (kind == F_DIR || kind == F_FD));
    bool link = id >= 256 && (kind >= F_FD_ENTRY || kind == F_EXE || kind == F_CWD);
    out->type = dir ? VN_DIR : link ? VN_LNK : VN_REG;
}

static process_t* live(i32 pid) {
    if (pid <= 0) return NULL;
    process_t* p = proc_by_pid(pid);
    return p && p->state != PROC_UNUSED ? p : NULL;
}

static bool parse_pid(const char* s, i32* out) {
    i32 v = 0;
    if (!*s) return false;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || v > 10000000) return false;
        v = v * 10 + (*s - '0');
    }
    *out = v;
    return true;
}

static i32 procfs_root(vnode_t* out) {
    make_node(P_ROOT, out);
    return 0;
}

static i32 procfs_lookup(const vnode_t* dir, const char* name, vnode_t* out) {
    if (dir->id == P_ROOT) {
        for (u64 id = P_SELF; id < P_GLOBAL_END; id++) {
            if (!strcmp(name, global_names[id])) {
                if (id == P_SELF) make_node((u64)proc_current()->pid << 8, out);
                else make_node(id, out);
                return 0;
            }
        }
        i32 pid;
        if (parse_pid(name, &pid) && live(pid)) {
            make_node((u64)pid << 8, out);
            return 0;
        }
        return -ENOENT;
    }
    if (dir->id >= 256 && (dir->id & 0xFF) == F_DIR) {
        if (!live((i32)(dir->id >> 8))) return -ENOENT;
        for (u64 k = F_STAT; k < F_END; k++) {
            if (!strcmp(name, proc_file_names[k])) {
                make_node(dir->id | k, out);
                return 0;
            }
        }
        return -ENOENT;
    }
    if (dir->id >= 256 && (dir->id & 0xFF) == F_FD) {
        process_t* p = live((i32)(dir->id >> 8));
        i32 fd;
        if (!p || !parse_pid(name, &fd) || fd >= MAX_FDS || !p->fds[fd]) return -ENOENT;
        make_node((dir->id & ~0xFFULL) | (u64)(F_FD_ENTRY + fd), out);
        return 0;
    }
    return -ENOTDIR;
}

static i32 procfs_readdir(const vnode_t* dir, u32 index, char* name_out, vnode_t* out) {
    if (dir->id == P_ROOT) {
        u32 nglobal = P_GLOBAL_END - P_SELF;
        if (index < nglobal) {
            u64 id = P_SELF + index;
            strcpy(name_out, global_names[id]);
            if (id == P_SELF) make_node((u64)proc_current()->pid << 8, out);
            else make_node(id, out);
            return 1;
        }
        u32 n = index - nglobal;
        for (u32 i = 1; i < MAX_PROCS; i++) {
            process_t* p = proc_slot(i);
            if (p->state == PROC_UNUSED) continue;
            if (n-- == 0) {
                ksnprintf(name_out, VFS_NAME_MAX, "%d", p->pid);
                make_node((u64)p->pid << 8, out);
                return 1;
            }
        }
        return 0;
    }
    if (dir->id >= 256 && (dir->id & 0xFF) == F_FD) {
        process_t* p = live((i32)(dir->id >> 8));
        if (!p) return 0;
        for (i32 fd = 0; fd < MAX_FDS; fd++) {
            if (!p->fds[fd] || index--) continue;
            ksnprintf(name_out, VFS_NAME_MAX, "%d", fd);
            make_node((dir->id & ~0xFFULL) | (u64)(F_FD_ENTRY + fd), out);
            return 1;
        }
        return 0;
    }
    if (dir->id >= 256 && (dir->id & 0xFF) == F_DIR) {
        u64 k = F_STAT + index;
        if (k >= F_END) return 0;
        strcpy(name_out, proc_file_names[k]);
        make_node(dir->id | k, out);
        return 1;
    }
    return -ENOTDIR;
}

// ---- generating the files ----

#define GEN_MAX 4096

static char state_char(const process_t* p) {
    switch (p->state) {
        case PROC_RUNNING: case PROC_READY: return 'R';
        case PROC_BLOCKED: return 'S';
        case PROC_STOPPED: return 'T';
        case PROC_ZOMBIE:  return 'Z';
        default:           return 'D';
    }
}

static const char* state_name(char c) {
    switch (c) {
        case 'R': return "running";
        case 'S': return "sleeping";
        case 'T': return "stopped";
        case 'Z': return "zombie";
        default:  return "disk sleep";
    }
}

// Copy up to len bytes of p's memory at va (another process's, possibly).
static u64 read_user_of(const process_t* p, u64 va, char* out, u64 len) {
    u64 done = 0;
    while (done < len) {
        u64 pte = vmm_get_pte(p->space, va);
        if (!(pte & 1)) break;
        u64 off = va & 0xFFF, chunk = 4096 - off;
        if (chunk > len - done) chunk = len - done;
        memcpy(out + done, (const void*)((pte & 0x000FFFFFFFFFF000ULL) + vmm_hhdm() + off), chunk);
        done += chunk;
        va += chunk;
    }
    return done;
}

// Linux's device numbers: tty1 is 4:1, pts/N is 136:N (packed for stat's
// tty_nr as minor | major << 8).
static i32 tty_nr(tty_t* t) {
    if (!t) return 0;
    u32 n = pty_index(t);
    return n == ~0u ? (4 << 8) | 1 : (136 << 8) | (i32)n;
}

static u64 gen_process(const process_t* p, u64 kind, char* b) {
    char st = state_char(p);
    u64 vsize = (u64)p->user_pages * 4096;
    switch (kind) {
        case F_STAT:
            return (u64)ksnprintf(b, GEN_MAX,
                "%d (%s) %c %d %d %d %d %d 0 0 0 0 0 %lu 0 0 0 20 0 1 0 %lu %lu %lu "
                "18446744073709551615 0 0 0 0 0 0 0 0 0 0 0 0 17 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n",
                p->pid, p->name, st, p->ppid, p->pgid, p->sid, tty_nr(p->ctty),
                p->ctty ? p->ctty->fg_pgid : -1, TO_HZ(p->cpu_ticks), TO_HZ(p->start_tick),
                vsize, (u64)p->user_pages);
        case F_STATUS:
            return (u64)ksnprintf(b, GEN_MAX,
                "Name:\t%s\nState:\t%c (%s)\nTgid:\t%d\nPid:\t%d\nPPid:\t%d\n"
                "Uid:\t0\t0\t0\t0\nGid:\t0\t0\t0\t0\nVmSize:\t%lu kB\nVmRSS:\t%lu kB\nThreads:\t1\n",
                p->name, st, state_name(st), p->pid, p->pid, p->ppid, vsize / 1024, vsize / 1024);
        case F_CMDLINE: {
            if (p->state == PROC_ZOMBIE || !p->arg_end || p->arg_end <= p->arg_start) return 0;
            u64 len = p->arg_end - p->arg_start;
            if (len > GEN_MAX) len = GEN_MAX;
            return read_user_of(p, p->arg_start, b, len);
        }
        case F_COMM:
            return (u64)ksnprintf(b, GEN_MAX, "%s\n", p->name);
        default:
            return 0;
    }
}

static u64 gen_global(u64 id, char* b) {
    u64 uptime_cs = (u64)ticks * 100 / 250;          // centiseconds
    u64 idle_cs = TO_HZ(proc_slot(0)->cpu_ticks);
    switch (id) {
        case P_BATTERY:
            return battery_read_text(b, GEN_MAX);
        case P_WIFI: {
            static const char* stages[]={"unprobed","absent","firmware-missing",
                "firmware-invalid","pci-bar","prepare","persistence","reset",
                "clock","mac-access","rfkill","dma","firmware-load","alive","nvm","ready"};
            iwx200_status_t st;iwx200_status(&st);
            const char* stage=(u32)st.stage<sizeof(stages)/sizeof(*stages)?stages[st.stage]:"unknown";
            return ksnprintf(b,GEN_MAX,
                "device: Intel AX200\nstage: %s\nerror: %d\npresent: %u\n"
                "firmware-staged: %u\nfirmware-alive: %u\nnvm-ready: %u\n"
                "firmware-api: %u\nlink: %u\ninterrupt: 0x%x\ngp-control: 0x%x\n"
                "fh-status: 0x%x\nrfkill-clear: %u\n",
                stage,st.error,st.present,st.firmware_staged,st.firmware_alive,
                st.nvm_ready,st.firmware_api,iwx200_link(),st.csr_interrupt,st.csr_gp,
                st.csr_fh_status,st.rfkill_clear);
        }
        case P_NETINFO: {
            net_info_t info;net_info(&info);char name[16];net_ifname(name);u8* ip=(u8*)&info.address;u8* dns=(u8*)&info.dns;
            return ksnprintf(b,GEN_MAX,"%s: up=%u link=%u mtu=%u ip=%u.%u.%u.%u dns=%u.%u.%u.%u\nrx=%llu tx=%llu rx_dropped=%llu tx_dropped=%llu\n",name,info.up,info.link,info.mtu,ip[0],ip[1],ip[2],ip[3],dns[0],dns[1],dns[2],dns[3],info.rx_packets,info.tx_packets,info.rx_dropped,info.tx_dropped);
        }
        case P_CACHE: {
            disk_cache_stats_t d;
            disk_cache_stats(&d);
            return (u64)ksnprintf(b, GEN_MAX,
                "block_cache_blocks: %u\nblock_cache_dirty: %u\nblock_cache_hits: %lu\n"
                "block_cache_misses: %lu\nblock_cache_writebacks: %lu\nblock_cache_errors: %lu\n"
                "file_cache_pages: %lu\n", d.blocks, d.dirty, d.hits, d.misses,
                d.writebacks, d.errors, pagecache_pages());
        }
        case P_MEMINFO: {
            u64 total = pmm_total_pages() * 4, free = pmm_count_free() * 4;
            disk_cache_stats_t cache;
            disk_cache_stats(&cache);
            return (u64)ksnprintf(b, GEN_MAX,
                "MemTotal:       %8lu kB\nMemFree:        %8lu kB\nMemAvailable:   %8lu kB\n"
                "Buffers:        %8lu kB\nCached:         %8lu kB\nSwapCached:            0 kB\n"
                "Active:                0 kB\nInactive:              0 kB\nSwapTotal:             0 kB\n"
                "SwapFree:              0 kB\nShmem:                 0 kB\nSlab:                  0 kB\n",
                total, free, free, (u64)cache.blocks * 4, pagecache_pages() * 4);
        }
        case P_UPTIME:
            return (u64)ksnprintf(b, GEN_MAX, "%lu.%02lu %lu.%02lu\n",
                                  uptime_cs / 100, uptime_cs % 100, idle_cs / 100, idle_cs % 100);
        case P_LOADAVG: {
            u32 running = 0, total = 0;
            i32 last = 0;
            for (u32 i = 1; i < MAX_PROCS; i++) {
                process_t* p = proc_slot(i);
                if (p->state == PROC_UNUSED) continue;
                total++;
                if (p->state == PROC_RUNNING || p->state == PROC_READY) running++;
                if (p->pid > last) last = p->pid;
            }
            return (u64)ksnprintf(b, GEN_MAX, "0.00 0.00 0.00 %u/%u %d\n", running, total, last);
        }
        case P_STAT: {
            u64 busy = 0;
            u32 procs = 0, running = 0, blocked = 0;
            for (u32 i = 1; i < MAX_PROCS; i++) {
                process_t* p = proc_slot(i);
                if (p->state == PROC_UNUSED) continue;
                busy += p->cpu_ticks;
                procs++;
                if (p->state == PROC_RUNNING || p->state == PROC_READY) running++;
                if (p->state == PROC_BLOCKED) blocked++;
            }
            return (u64)ksnprintf(b, GEN_MAX,
                "cpu  %lu 0 0 %lu 0 0 0 0 0 0\ncpu0 %lu 0 0 %lu 0 0 0 0 0 0\n"
                "btime %lu\nprocesses %u\nprocs_running %u\nprocs_blocked %u\n",
                TO_HZ(busy), idle_cs, TO_HZ(busy), idle_cs,
                rtc_now() - (u64)ticks / 250, procs, running, blocked);
        }
        case P_VERSION:
            return (u64)ksnprintf(b, GEN_MAX,
                "Linux version 6.1.0-primemeridian (" KUNIX_BUILD_DESCRIPTION ")\n");
        case P_MOUNTS: {
            u64 n = 0;
            for (u32 i = 0; ; i++) {
                const mount_t* m = vfs_mount_at(i);
                if (!m) break;
                n += (u64)ksnprintf(b + n, GEN_MAX - n, "%s %s %s rw 0 0\n",
                                    m->ops->type, m->point, m->ops->type);
                if (n >= GEN_MAX) return GEN_MAX - 1;
            }
            return n;
        }
        case P_CPUINFO: {
            char vendor[13], brand[49];
            cpuid_vendor(vendor);
            cpuid_brand(brand);
            return (u64)ksnprintf(b, GEN_MAX,
                "processor\t: 0\nvendor_id\t: %s\nmodel name\t: %s\ncpu MHz\t\t: 0.000\n"
                "flags\t\t: fpu tsc cx8 cmov mmx fxsr sse sse2 syscall nx lm\n\n",
                vendor, brand[0] ? brand : "unknown");
        }
        case P_FILESYSTEMS:
            return (u64)ksnprintf(b, GEN_MAX, "\tfat32\n\tufs2\nnodev\tdevfs\nnodev\tproc\n");
        default:
            return 0;
    }
}

static i64 procfs_read(vnode_t* node, u64 off, void* buf, u64 len) {
    // A nondestructive view, like the other generated proc files. Avoid
    // GEN_MAX: the boot log can be much larger than a single text page.
    if (node->id == P_KMSG) return (i64)kmsg_read(off, buf, len);
    char* b = kmalloc(GEN_MAX);
    if (!b) return -ENOMEM;
    u64 n;
    if (node->id >= 256) {
        process_t* p = live((i32)(node->id >> 8));
        if (!p) { kfree(b); return -ESRCH; }
        n = gen_process(p, node->id & 0xFF, b);
    } else {
        n = gen_global(node->id, b);
    }
    if (n > GEN_MAX) n = GEN_MAX;
    i64 r = 0;
    if (off < n) {
        r = (i64)(n - off < len ? n - off : len);
        memcpy(buf, b + off, (u64)r);
    }
    kfree(b);
    return r;
}

// fd/N, exe and cwd are symbolic links, as on Linux: to the file's path
// (or pipe:[...] for a pipe), the program, the working directory.
static i64 procfs_readlink(vnode_t* node, char* buf, u64 len) {
    process_t* p = live((i32)(node->id >> 8));
    if (!p || node->id < 256) return -ENOENT;
    u64 kind = node->id & 0xFF;
    char tmp[VFS_PATH_MAX];
    const char* s = tmp;
    if (kind == F_EXE) s = p->exe;
    else if (kind == F_CWD) s = p->cwd;
    else if (kind >= F_FD_ENTRY && kind - F_FD_ENTRY < MAX_FDS) {
        struct file* f = p->fds[kind - F_FD_ENTRY];
        if (!f) return -ENOENT;
        if (f->path) s = f->path;
        else ksnprintf(tmp, sizeof(tmp), "%s:[%lu]", f->node && f->node->type == VN_SOCK ? "socket" : "pipe",
                       (u64)f->node >> 4);
    } else {
        return -EINVAL;
    }
    u64 n = strlen(s);
    if (n > len) n = len;
    memcpy(buf, s, n);
    return (i64)n;
}

static i32 procfs_follow(vnode_t* link, vnode_t* out) {
    process_t* p = live((i32)(link->id >> 8));
    u64 kind = link->id & 0xFF;
    if (!p || link->id < 256 || kind < F_FD_ENTRY || kind - F_FD_ENTRY >= MAX_FDS) return -EINVAL;
    struct file* f = p->fds[kind - F_FD_ENTRY];
    if (!f || !f->node) return -ENOENT;
    *out = *f->node;
    return 0;
}

const vfs_ops_t procfs_vfs_ops = {
    .readlink = procfs_readlink,
    .follow = procfs_follow,
    .type = "proc",
    .root = procfs_root,
    .lookup = procfs_lookup,
    .read = procfs_read,
    .readdir = procfs_readdir,
};
