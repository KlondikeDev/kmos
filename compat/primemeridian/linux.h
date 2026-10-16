// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

// The Linux x86_64 user ABI, as Prime Meridian needs it: system call
// numbers, and the layout of every structure that crosses the boundary.
// Names carry an l_ prefix where they would collide with Kunix's own.

#include <types.h>

// ---- system call numbers (arch/x86/entry/syscalls/syscall_64.tbl) ----
#define LNX_read              0
#define LNX_write             1
#define LNX_open              2
#define LNX_close             3
#define LNX_stat              4
#define LNX_fstat             5
#define LNX_lstat             6
#define LNX_poll              7
#define LNX_lseek             8
#define LNX_mmap              9
#define LNX_mprotect          10
#define LNX_munmap            11
#define LNX_brk               12
#define LNX_rt_sigaction      13
#define LNX_rt_sigprocmask    14
#define LNX_rt_sigreturn      15
#define LNX_ioctl             16
#define LNX_pread64           17
#define LNX_pwrite64          18
#define LNX_readv             19
#define LNX_writev            20
#define LNX_access            21
#define LNX_pipe              22
#define LNX_select            23
#define LNX_sched_yield       24
#define LNX_mremap            25
#define LNX_msync             26
#define LNX_madvise           28
#define LNX_dup               32
#define LNX_dup2              33
#define LNX_pause             34
#define LNX_nanosleep         35
#define LNX_getitimer         36
#define LNX_alarm             37
#define LNX_setitimer         38
#define LNX_getpid            39
#define LNX_sendfile          40
#define LNX_socket            41
#define LNX_clone             56
#define LNX_fork              57
#define LNX_vfork             58
#define LNX_execve            59
#define LNX_exit              60
#define LNX_wait4             61
#define LNX_kill              62
#define LNX_uname             63
#define LNX_fcntl             72
#define LNX_flock             73
#define LNX_fsync             74
#define LNX_fdatasync         75
#define LNX_truncate          76
#define LNX_ftruncate         77
#define LNX_getdents          78
#define LNX_getcwd            79
#define LNX_chdir             80
#define LNX_fchdir            81
#define LNX_rename            82
#define LNX_mkdir             83
#define LNX_rmdir             84
#define LNX_creat             85
#define LNX_link              86
#define LNX_unlink            87
#define LNX_symlink           88
#define LNX_readlink          89
#define LNX_chmod             90
#define LNX_fchmod            91
#define LNX_chown             92
#define LNX_fchown            93
#define LNX_lchown            94
#define LNX_umask             95
#define LNX_gettimeofday      96
#define LNX_getrlimit         97
#define LNX_getrusage         98
#define LNX_sysinfo           99
#define LNX_times             100
#define LNX_getuid            102
#define LNX_getgid            104
#define LNX_setuid            105
#define LNX_setgid            106
#define LNX_geteuid           107
#define LNX_getegid           108
#define LNX_setpgid           109
#define LNX_getppid           110
#define LNX_getpgrp           111
#define LNX_setsid            112
#define LNX_setreuid          113
#define LNX_setregid          114
#define LNX_getgroups         115
#define LNX_setgroups         116
#define LNX_setresuid         117
#define LNX_getresuid         118
#define LNX_setresgid         119
#define LNX_getresgid         120
#define LNX_getpgid           121
#define LNX_setfsuid          122
#define LNX_setfsgid          123
#define LNX_getsid            124
#define LNX_rt_sigpending     127
#define LNX_rt_sigtimedwait   128
#define LNX_rt_sigsuspend     130
#define LNX_sigaltstack       131
#define LNX_utime             132
#define LNX_mknod             133
#define LNX_personality       135
#define LNX_statfs            137
#define LNX_fstatfs           138
#define LNX_getpriority       140
#define LNX_setpriority       141
#define LNX_sched_getparam    143
#define LNX_sched_getscheduler 145
#define LNX_mlock             149
#define LNX_munlock           150
#define LNX_prctl             157
#define LNX_arch_prctl        158
#define LNX_setrlimit         160
#define LNX_sync              162
#define LNX_reboot            169
#define LNX_sethostname       170
#define LNX_gettid            186
#define LNX_tkill             200
#define LNX_time              201
#define LNX_futex             202
#define LNX_sched_getaffinity 204
#define LNX_getdents64        217
#define LNX_set_tid_address   218
#define LNX_fadvise64         221
#define LNX_clock_gettime     228
#define LNX_clock_getres      229
#define LNX_clock_nanosleep   230
#define LNX_exit_group        231
#define LNX_tgkill            234
#define LNX_utimes            235
#define LNX_waitid            247
#define LNX_openat            257
#define LNX_mkdirat           258
#define LNX_fchownat          260
#define LNX_newfstatat        262
#define LNX_unlinkat          263
#define LNX_renameat          264
#define LNX_linkat            265
#define LNX_symlinkat         266
#define LNX_readlinkat        267
#define LNX_fchmodat          268
#define LNX_faccessat         269
#define LNX_pselect6          270
#define LNX_ppoll             271
#define LNX_set_robust_list   273
#define LNX_utimensat         280
#define LNX_dup3              292
#define LNX_pipe2             293
#define LNX_prlimit64         302
#define LNX_renameat2         316
#define LNX_getrandom         318
#define LNX_statx             332
#define LNX_rseq              334
#define LNX_clone3            435
#define LNX_faccessat2        439

// ---- *at() ----
#define L_AT_FDCWD            (-100)
#define L_AT_SYMLINK_NOFOLLOW 0x100
#define L_AT_REMOVEDIR        0x200
#define L_AT_EMPTY_PATH       0x1000

// ---- files ----
typedef struct {
    u64 st_dev;
    u64 st_ino;
    u64 st_nlink;
    u32 st_mode;
    u32 st_uid;
    u32 st_gid;
    u32 __pad0;
    u64 st_rdev;
    i64 st_size;
    i64 st_blksize;
    i64 st_blocks;
    i64 st_atime, st_atime_nsec;
    i64 st_mtime, st_mtime_nsec;
    i64 st_ctime, st_ctime_nsec;
    i64 __unused[3];
} l_stat_t;

typedef struct {
    u64 d_ino;
    i64 d_off;
    u16 d_reclen;
    u8  d_type;
    char d_name[];
} PACKED l_dirent64_t;

#define L_DT_FIFO 1
#define L_DT_CHR  2
#define L_DT_DIR  4
#define L_DT_REG  8
#define L_DT_LNK  10
#define L_DT_SOCK 12

typedef struct {
    u64 iov_base;
    u64 iov_len;
} l_iovec_t;

typedef struct {
    i32 fd;
    i16 events;
    i16 revents;
} l_pollfd_t;

typedef struct {
    i64 f_type, f_bsize;
    u64 f_blocks, f_bfree, f_bavail, f_files, f_ffree;
    i32 f_fsid[2];
    i64 f_namelen, f_frsize, f_flags;
    i64 f_spare[4];
} l_statfs_t;

#define L_F_DUPFD         0
#define L_F_GETFD         1
#define L_F_SETFD         2
#define L_F_GETFL         3
#define L_F_SETFL         4
#define L_F_GETLK         5
#define L_F_SETLK         6
#define L_F_SETLKW        7
#define L_F_DUPFD_CLOEXEC 1030
#define L_FD_CLOEXEC      1

// ---- time ----
typedef struct { i64 tv_sec; i64 tv_nsec; } l_timespec_t;
typedef struct { i64 tv_sec; i64 tv_usec; } l_timeval_t;
typedef struct { l_timeval_t it_interval, it_value; } l_itimerval_t;
typedef struct { i64 tms_utime, tms_stime, tms_cutime, tms_cstime; } l_tms_t;

#define L_CLOCK_REALTIME           0
#define L_CLOCK_MONOTONIC          1
#define L_CLOCK_PROCESS_CPUTIME_ID 2
#define L_CLOCK_THREAD_CPUTIME_ID  3
#define L_CLOCK_MONOTONIC_RAW      4
#define L_CLOCK_REALTIME_COARSE    5
#define L_CLOCK_MONOTONIC_COARSE   6
#define L_CLOCK_BOOTTIME           7

// ---- processes ----
typedef struct {
    char sysname[65], nodename[65], release[65], version[65], machine[65], domainname[65];
} l_utsname_t;

typedef struct { u64 rlim_cur, rlim_max; } l_rlimit_t;
#define L_RLIM_INFINITY (~0ULL)
#define L_RLIMIT_STACK  3
#define L_RLIMIT_NOFILE 7

typedef struct {
    l_timeval_t ru_utime, ru_stime;
    i64 ru_maxrss, ru_ixrss, ru_idrss, ru_isrss, ru_minflt, ru_majflt, ru_nswap,
        ru_inblock, ru_oublock, ru_msgsnd, ru_msgrcv, ru_nsignals, ru_nvcsw, ru_nivcsw;
} l_rusage_t;

typedef struct {
    i64 uptime;
    u64 loads[3];
    u64 totalram, freeram, sharedram, bufferram, totalswap, freeswap;
    u16 procs, pad;
    u32 pad2;
    u64 totalhigh, freehigh;
    u32 mem_unit;
    char _f[4];
} l_sysinfo_t;

#define L_ARCH_SET_GS 0x1001
#define L_ARCH_SET_FS 0x1002
#define L_ARCH_GET_FS 0x1003
#define L_ARCH_GET_GS 0x1004

#define L_PR_SET_NAME 15
#define L_PR_GET_NAME 16

#define L_FUTEX_WAIT 0
#define L_FUTEX_WAKE 1
#define L_FUTEX_PRIVATE_FLAG 128

// ---- terminals ----
#define L_TCGETS     0x5401
#define L_TCSETS     0x5402
#define L_TCSETSW    0x5403
#define L_TCSETSF    0x5404
#define L_TCSBRK     0x5409
#define L_TCXONC     0x540A
#define L_TCFLSH     0x540B
#define L_TIOCSCTTY  0x540E
#define L_TIOCGPGRP  0x540F
#define L_TIOCSPGRP  0x5410
#define L_TIOCOUTQ   0x5411
#define L_TIOCGWINSZ 0x5413
#define L_TIOCSWINSZ 0x5414
#define L_FIONREAD   0x541B
#define L_TIOCNOTTY  0x5422
#define L_FIONBIO    0x5421
#define L_TIOCGSID   0x5429
#define L_FIONCLEX   0x5450
#define L_FIOCLEX    0x5451
#define L_TIOCGPTN   0x80045430
#define L_TIOCSPTLCK 0x40045431

typedef struct { u16 ws_row, ws_col, ws_xpixel, ws_ypixel; } l_winsize_t;

// ---- signals ----
#define L_SA_NOCLDSTOP 0x00000001
#define L_SA_SIGINFO   0x00000004
#define L_SA_ONSTACK   0x08000000
#define L_SA_RESTART   0x10000000
#define L_SA_NODEFER   0x40000000
#define L_SA_RESETHAND 0x80000000
#define L_SA_RESTORER  0x04000000

typedef struct {
    u64 handler;
    u64 flags;
    u64 restorer;
    u64 mask;
} l_sigaction_t;

typedef struct {
    u64 ss_sp;
    i32 ss_flags;
    u32 __pad;
    u64 ss_size;
} l_stack_t;
#define L_SS_DISABLE 2

typedef struct {
    u64 r8, r9, r10, r11, r12, r13, r14, r15;
    u64 rdi, rsi, rbp, rbx, rdx, rax, rcx, rsp, rip, eflags;
    u16 cs, gs, fs, ss;
    u64 err, trapno, oldmask, cr2;
    u64 fpstate;                    // -> 512-byte fxsave image, or 0
    u64 reserved1[8];
} l_sigcontext_t;

typedef struct {
    u64 uc_flags;
    u64 uc_link;
    l_stack_t uc_stack;
    l_sigcontext_t uc_mcontext;
    u64 uc_sigmask;
} l_ucontext_t;

typedef struct {
    i32 si_signo;
    i32 si_errno;
    i32 si_code;
    i32 __pad;
    union {
        u8 raw[112];
        struct { i32 pid; u32 uid; i32 status; } chld;
        struct { i32 pid; u32 uid; } kill;
        struct { u64 addr; } fault;
    } u;
} l_siginfo_t;

// What the stack holds when a Linux handler starts: rsp points at pretcode.
typedef struct {
    u64 pretcode;                   // the handler's return address: sa_restorer
    l_ucontext_t uc;
    l_siginfo_t info;
} l_rt_sigframe_t;

// Linux sigsets have signal n at bit n-1; Kunix masks have it at bit n.
static inline u32 l_sigset_to_mask(u64 set) {
    return (u32)(set << 1) & ~1u;
}
static inline u64 l_mask_to_sigset(u32 mask) {
    return (u64)(mask >> 1);
}
