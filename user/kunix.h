// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// Userland runtime for ring 3 Kunix programs. There is no libc: this header,
// crt0.s and the linker script in user/user.ld are the whole runtime.
// Programs define main(int argc, char** argv); its return value becomes the
// exit status.
//
// Syscalls go through int 0x80: number in rax, arguments in rdi, rsi, rdx,
// result in rax (negative means failure). The kernel's trap stub saves and
// restores every register and only rax is written back, so rax is the sole
// clobber.

#pragma once

typedef unsigned long u64;
typedef long          i64;

#define KUNIX_STDIN  0
#define KUNIX_STDOUT 1
#define KUNIX_STDERR 2

// Must match include/usermode.h.
#define SYS_EXIT     0
#define SYS_WRITE    1
#define SYS_GETCPL   2
#define SYS_READ     3
#define SYS_READFILE 4
#define SYS_SPAWN    5
#define SYS_WAIT     6
#define SYS_GETPID   7
#define SYS_SBRK     8
#define SYS_YIELD    9
#define SYS_SLEEP    10
#define SYS_READDIR  11
#define SYS_CHDIR    12
#define SYS_GETCWD   13
#define SYS_PROCINFO 14

static inline long syscall3(long n, long a, long b, long c) {
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(n), "D"(a), "S"(b), "d"(c)
                     : "memory");
    return ret;
}

// ---- syscalls ----

__attribute__((noreturn))
static inline void sys_exit(long status) {
    syscall3(SYS_EXIT, status, 0, 0);
    __builtin_unreachable();
}

// write(fd, buf, len) -> bytes written, or -1. fd 1 or 2 (both the screen).
static inline long sys_write(long fd, const void* buf, u64 len) {
    return syscall3(SYS_WRITE, fd, (long)buf, (long)len);
}

// read(fd, buf, len) -> bytes read, or -1. Blocks until Enter is pressed.
// Returns the line without its newline.
static inline long sys_read(long fd, void* buf, u64 len) {
    return syscall3(SYS_READ, fd, (long)buf, (long)len);
}

// readfile(path, buf, len) -> bytes read, or -1 if the file isn't found.
static inline long sys_readfile(const char* path, void* buf, u64 len) {
    return syscall3(SYS_READFILE, (long)path, (long)buf, (long)len);
}

// getcpl() -> the privilege level we are running at. Should always be 3.
static inline long sys_getcpl(void) {
    return syscall3(SYS_GETCPL, 0, 0, 0);
}

// spawn(path, argv) -> pid of a new process running the ELF at path, or -1.
// argv is NULL-terminated; argv[0] is conventionally the program name.
static inline long sys_spawn(const char* path, char* const* argv) {
    return syscall3(SYS_SPAWN, (long)path, (long)argv, 0);
}

// wait(pid, &status) -> pid of the child that exited, or -1 if there is no
// such child. pid -1 means any child. Blocks until one exits.
static inline long sys_wait(long pid, long* status) {
    return syscall3(SYS_WAIT, pid, (long)status, 0);
}

static inline long sys_getpid(void) {
    return syscall3(SYS_GETPID, 0, 0, 0);
}

// sbrk(increment) -> the old program break, or -1. Grows the heap.
static inline void* sys_sbrk(long increment) {
    return (void*)syscall3(SYS_SBRK, increment, 0, 0);
}

static inline void sys_yield(void) {
    syscall3(SYS_YIELD, 0, 0, 0);
}

static inline void sys_sleep(u64 ms) {
    syscall3(SYS_SLEEP, (long)ms, 0, 0);
}

typedef struct {
    char name[64];
    unsigned char is_dir;
    unsigned int size;
} kunix_dirent_t;

// readdir(path, index, &ent) -> 1 if entry `index` exists, 0 past the end,
// -1 if path is not a directory. path NULL or "" means the working directory.
static inline long sys_readdir(const char* path, u64 index, kunix_dirent_t* ent) {
    return syscall3(SYS_READDIR, (long)path, (long)index, (long)ent);
}

static inline long sys_chdir(const char* path) {
    return syscall3(SYS_CHDIR, (long)path, 0, 0);
}

static inline long sys_getcwd(char* buf, u64 len) {
    return syscall3(SYS_GETCWD, (long)buf, (long)len, 0);
}

typedef struct {
    int pid;
    int ppid;
    int state;        // see KUNIX_PROC_* below
    char name[32];
} kunix_procinfo_t;

#define KUNIX_PROC_READY   2
#define KUNIX_PROC_RUNNING 3
#define KUNIX_PROC_BLOCKED 4
#define KUNIX_PROC_ZOMBIE  5

// procinfo(index, &info) -> 1 if there is an index-th process, else 0.
static inline long sys_procinfo(u64 index, kunix_procinfo_t* info) {
    return syscall3(SYS_PROCINFO, (long)index, (long)info, 0);
}

// ---- small conveniences, since there is no string.h or stdio.h ----

static inline u64 kstrlen(const char* s) {
    u64 n = 0;
    while (s[n]) n++;
    return n;
}

static inline int kstrcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static inline void kputs(const char* s) {
    sys_write(KUNIX_STDOUT, s, kstrlen(s));
}

static inline void kputu(u64 v) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    do { buf[--i] = (char)('0' + v % 10); v /= 10; } while (v);
    kputs(buf + i);
}

static inline void kputi(long v) {
    if (v < 0) { kputs("-"); kputu((u64)-v); }
    else kputu((u64)v);
}

// Bump allocator on top of sbrk. Nothing is ever freed.
static inline void* kalloc(u64 size) {
    size = (size + 15) & ~15UL;
    void* p = sys_sbrk((long)size);
    return (long)p == -1 ? 0 : p;
}
