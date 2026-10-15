// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>
#include <idt.h>
#include <vfs.h>

// The native Kunix syscall ABI.
//
// int 0x80 with the number in rax and arguments in rdi, rsi, rdx, r10, r8,
// r9 (the System V order, with r10 standing in for rcx as Linux does). The
// result comes back in rax: >= 0 on success, -errno on failure. Only rax is
// modified.
//
// Personalities (Prime Meridian for Linux, Inferno for the BSDs) will
// translate their own numbers and structures onto these. Must match
// user/kunix.h.

#define SYS_EXIT        0    // (code)
#define SYS_WRITE       1    // (fd, buf, len)
#define SYS_GETCPL      2    // () -> current privilege level, for tests
#define SYS_READ        3    // (fd, buf, len)
#define SYS_OPEN        4    // (path, flags, mode)
#define SYS_CLOSE       5    // (fd)
#define SYS_SPAWN       6    // (path, argv, envp) -> pid
#define SYS_WAITPID     7    // (pid, &status, options)
#define SYS_GETPID      8
#define SYS_SBRK        9    // (increment) -> old break
#define SYS_YIELD       10
#define SYS_SLEEP       11   // (milliseconds)
#define SYS_READDIR     12   // (fd, kdirent_t*) -> 1 entry, 0 end
#define SYS_CHDIR       13   // (path)
#define SYS_GETCWD      14   // (buf, len)
#define SYS_PROCINFO    15   // (index, proc_info_t*) -> 1 found, 0 end
#define SYS_LSEEK       16   // (fd, offset, whence)
#define SYS_FSTAT       17   // (fd, kstat_t*)
#define SYS_STAT        18   // (path, kstat_t*)
#define SYS_DUP         19   // (fd)
#define SYS_DUP2        20   // (oldfd, newfd)
#define SYS_PIPE        21   // (int fds[2])
#define SYS_MKDIR       22   // (path)
#define SYS_UNLINK      23   // (path)
#define SYS_RMDIR       24   // (path)
#define SYS_KILL        25   // (pid, sig); pid < 0 means process group -pid
#define SYS_SIGACTION   26   // (sig, handler, restorer) -> old handler
#define SYS_SIGRETURN   27
#define SYS_GETPPID     28
#define SYS_SETPGID     29   // (pid, pgid); 0 means self
#define SYS_GETPGID     30   // (pid)
#define SYS_TCSETPGRP   31   // (fd, pgid)
#define SYS_TCGETPGRP   32   // (fd)
#define SYS_FORK        33
#define SYS_EXECVE      34   // (path, argv, envp)
#define SYS_UPTIME      35   // () -> milliseconds since boot
#define SYS_IOCTL       36   // (fd, request, arg)
#define SYS_FTRUNCATE   37   // (fd, length)
#define SYS_SIGPROCMASK 38   // (how, set) -> old mask
#define SYS_UNAME       39   // (kutsname_t*)
#define SYS_MEMINFO     40   // (kmeminfo_t*)
#define SYS_TIME        41   // () -> seconds since 1970 (UTC)
#define SYS_RENAME      42   // (oldpath, newpath)
#define SYS_COUNT       43

// ioctl requests on the terminal.
#define TIOCGMODE 1          // -> TTY_MODE_* bits
#define TIOCSMODE 2          // arg = TTY_MODE_* bits

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

// File types in kstat_t.st_mode, as everywhere.
#define S_IFMT  0170000
#define S_IFIFO 0010000
#define S_IFCHR 0020000
#define S_IFDIR 0040000
#define S_IFREG 0100000

typedef struct {
    u64 st_dev;
    u64 st_ino;
    u32 st_mode;
    u32 st_nlink;
    u64 st_size;
} kstat_t;

typedef struct {
    u64 d_ino;
    u64 d_size;
    u32 d_type;              // VN_* from vfs.h
    char d_name[VFS_NAME_MAX];
} kdirent_t;

typedef struct {
    char sysname[32];
    char release[32];
    char version[64];
    char machine[32];
} kutsname_t;

typedef struct {
    u64 total_pages;         // physical, 4 KiB each
    u64 free_pages;
    u64 heap_used;           // kernel heap, bytes
    u64 heap_free;
} kmeminfo_t;

// Called by isr_handler for vector 0x80.
void syscall_dispatch(registers_t* regs);
