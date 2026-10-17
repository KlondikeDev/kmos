// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// libk: the small C library ring 3 Kunix programs link against
// (user/lib/libk.c), plus crt0.s for the entry point. Programs define
// main(int argc, char** argv, char** envp); its return value is the exit
// status.
//
// The POSIX-named calls behave like their Unix namesakes: -1 and errno on
// failure. The raw syscall numbers and structures must match the kernel's
// include/syscall.h.

#pragma once

#include <stdarg.h>

typedef unsigned long u64;
typedef long          i64;
typedef unsigned long size_t;
typedef long          ssize_t;
typedef long          off_t;
typedef int           pid_t;

#define NULL ((void*)0)

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// ---- syscall numbers (include/syscall.h) ----
#define SYS_EXIT        0
#define SYS_WRITE       1
#define SYS_GETCPL      2
#define SYS_READ        3
#define SYS_OPEN        4
#define SYS_CLOSE       5
#define SYS_SPAWN       6
#define SYS_WAITPID     7
#define SYS_GETPID      8
#define SYS_SBRK        9
#define SYS_YIELD       10
#define SYS_SLEEP       11
#define SYS_READDIR     12
#define SYS_CHDIR       13
#define SYS_GETCWD      14
#define SYS_PROCINFO    15
#define SYS_LSEEK       16
#define SYS_FSTAT       17
#define SYS_STAT        18
#define SYS_DUP         19
#define SYS_DUP2        20
#define SYS_PIPE        21
#define SYS_MKDIR       22
#define SYS_UNLINK      23
#define SYS_RMDIR       24
#define SYS_KILL        25
#define SYS_SIGACTION   26
#define SYS_SIGRETURN   27
#define SYS_GETPPID     28
#define SYS_SETPGID     29
#define SYS_GETPGID     30
#define SYS_TCSETPGRP   31
#define SYS_TCGETPGRP   32
#define SYS_FORK        33
#define SYS_EXECVE      34
#define SYS_UPTIME      35
#define SYS_IOCTL       36
#define SYS_FTRUNCATE   37
#define SYS_SIGPROCMASK 38
#define SYS_UNAME       39
#define SYS_MEMINFO     40
#define SYS_TIME        41
#define SYS_RENAME      42
#define SYS_MMAP        43
#define SYS_MUNMAP      44
#define SYS_MPROTECT    45
#define SYS_SYNC        46
#define SYS_FSYNC       47

long syscall3(long n, long a, long b, long c);
long syscall6(long n, long a, long b, long c, long d, long e, long f);

// ---- errno ----
extern int errno;
#define EPERM         1
#define ENOENT        2
#define ESRCH         3
#define EINTR         4
#define EIO           5
#define ENXIO         6
#define E2BIG         7
#define ENOEXEC       8
#define EBADF         9
#define ECHILD       10
#define EAGAIN       11
#define ENOMEM       12
#define EACCES       13
#define EFAULT       14
#define EBUSY        16
#define EEXIST       17
#define EXDEV        18
#define ENODEV       19
#define ENOTDIR      20
#define EISDIR       21
#define EINVAL       22
#define EMFILE       24
#define ENOTTY       25
#define ENOSPC       28
#define ESPIPE       29
#define EROFS        30
#define EPIPE        32
#define ERANGE       34
#define ENAMETOOLONG 36
#define ENOSYS       38
#define ENOTEMPTY    39
const char* strerror(int err);

// ---- files ----
#define O_RDONLY    00
#define O_WRONLY    01
#define O_RDWR      02
#define O_CREAT     0100
#define O_EXCL      0200
#define O_TRUNC     01000
#define O_APPEND    02000
#define O_NONBLOCK  04000
#define O_DIRECTORY 0200000
#define O_CLOEXEC   02000000

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define S_IFMT  0170000
#define S_IFIFO 0010000
#define S_IFCHR 0020000
#define S_IFBLK 0060000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)

struct stat {
    u64 st_dev;
    u64 st_ino;
    unsigned int st_mode;
    unsigned int st_nlink;
    u64 st_size;
};

#define DT_REG  1
#define DT_DIR  2
#define DT_CHR  3
#define DT_FIFO 4

struct kdirent {
    u64 d_ino;
    u64 d_size;
    unsigned int d_type;
    char d_name[64];
};

int open(const char* path, int flags, ...);
int close(int fd);
ssize_t read(int fd, void* buf, size_t len);
ssize_t write(int fd, const void* buf, size_t len);
off_t lseek(int fd, off_t off, int whence);
int fstat(int fd, struct stat* st);
int stat(const char* path, struct stat* st);
int dup(int fd);
int dup2(int oldfd, int newfd);
int pipe(int fds[2]);
int mkdir(const char* path, int mode);
int unlink(const char* path);
int rmdir(const char* path);
int rename(const char* oldpath, const char* newpath);
int ftruncate(int fd, off_t len);
int chdir(const char* path);
char* getcwd(char* buf, size_t len);
// Next entry of a directory opened with O_DIRECTORY: 1, 0 at the end, -1.
int readdir_fd(int fd, struct kdirent* ent);
int isatty(int fd);

// Terminal modes (ioctl on the tty).
#define TIOCGMODE 1
#define TIOCSMODE 2
#define TTY_MODE_CANON 0x1
#define TTY_MODE_ECHO  0x2
#define TTY_MODE_ISIG  0x4
int ioctl(int fd, unsigned long req, unsigned long arg);

// ---- processes ----
#define WNOHANG   1
#define WUNTRACED 2
#define WIFEXITED(s)   (((s) & 0x7F) == 0)
#define WEXITSTATUS(s) (((s) >> 8) & 0xFF)
#define WIFSIGNALED(s) (((s) & 0x7F) != 0 && ((s) & 0x7F) != 0x7F)
#define WTERMSIG(s)    ((s) & 0x7F)
#define WIFSTOPPED(s)  (((s) & 0xFF) == 0x7F)
#define WSTOPSIG(s)    (((s) >> 8) & 0xFF)

__attribute__((noreturn)) void _exit(int status);
__attribute__((noreturn)) void exit(int status);
pid_t fork(void);
int execve(const char* path, char* const argv[], char* const envp[]);
int execvp(const char* file, char* const argv[]);      // searches $PATH
pid_t spawn(const char* path, char* const argv[], char* const envp[]);
pid_t waitpid(pid_t pid, int* status, int options);
pid_t wait(int* status);
pid_t getpid(void);
pid_t getppid(void);
int setpgid(pid_t pid, pid_t pgid);
pid_t getpgid(pid_t pid);
int tcsetpgrp(int fd, pid_t pgid);
pid_t tcgetpgrp(int fd);
void* sbrk(long increment);
void sched_yield(void);
unsigned int sleep(unsigned int seconds);
int msleep(unsigned long ms);
unsigned long uptime_ms(void);
long getcpl(void);

typedef struct {
    int pid;
    int ppid;
    int state;
    char name[32];
    int pgid;
} procinfo_t;
#define PROC_READY   2
#define PROC_RUNNING 3
#define PROC_BLOCKED 4
#define PROC_ZOMBIE  5
#define PROC_STOPPED 6
int procinfo(unsigned long index, procinfo_t* info);   // 1, or 0 at the end

struct utsname {
    char sysname[32];
    char release[32];
    char version[64];
    char machine[32];
};
int uname(struct utsname* u);

struct meminfo {
    unsigned long total_pages;
    unsigned long free_pages;
    unsigned long heap_used;
    unsigned long heap_free;
};
int meminfo(struct meminfo* m);

// ---- memory mappings ----
#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4
#define MAP_SHARED          0x01
#define MAP_PRIVATE         0x02
#define MAP_FIXED           0x10
#define MAP_ANONYMOUS       0x20
#define MAP_ANON            MAP_ANONYMOUS
#define MAP_POPULATE        0x8000
#define MAP_FIXED_NOREPLACE 0x100000
#define MAP_FAILED ((void*)-1)
void* mmap(void* addr, size_t len, int prot, int flags, int fd, off_t off);
int munmap(void* addr, size_t len);
int mprotect(void* addr, size_t len, int prot);
int fsync(int fd);
int sync(void);

typedef long time_t;
time_t time(time_t* out);             // seconds since 1970, UTC
struct tm {
    int tm_sec, tm_min, tm_hour;      // 0-59, 0-59, 0-23
    int tm_mday, tm_mon, tm_year;     // 1-31, 0-11, years since 1900
    int tm_wday, tm_yday;             // 0-6 (Sunday = 0), 0-365
};
struct tm* gmtime_r(const time_t* t, struct tm* out);

// ---- signals ----
#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGILL   4
#define SIGTRAP  5
#define SIGABRT  6
#define SIGBUS   7
#define SIGFPE   8
#define SIGKILL  9
#define SIGUSR1  10
#define SIGSEGV  11
#define SIGUSR2  12
#define SIGPIPE  13
#define SIGALRM  14
#define SIGTERM  15
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19
#define SIGTSTP  20
#define SIGTTIN  21
#define SIGTTOU  22
#define NSIG     32

typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

sighandler_t signal(int sig, sighandler_t handler);
int kill(pid_t pid, int sig);
int raise(int sig);
// Returns the old mask; sigprocmask-shaped but on a plain bit mask.
long sigmask(int how, unsigned long set);
const char* signame(int sig);

// ---- environment ----
extern char** environ;
char* getenv(const char* name);
int setenv(const char* name, const char* value, int overwrite);
int unsetenv(const char* name);

// ---- memory ----
void* malloc(size_t size);
void* calloc(size_t n, size_t size);
void* realloc(void* p, size_t size);
void free(void* p);

// ---- strings ----
size_t strlen(const char* s);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t n);
char* strcpy(char* d, const char* s);
char* strncpy(char* d, const char* s, size_t n);
char* strcat(char* d, const char* s);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
char* strstr(const char* hay, const char* needle);
char* strdup(const char* s);
void* memcpy(void* d, const void* s, size_t n);
void* memmove(void* d, const void* s, size_t n);
void* memset(void* d, int c, size_t n);
int memcmp(const void* a, const void* b, size_t n);
int atoi(const char* s);
long strtol(const char* s, char** end, int base);
int isdigit(int c);
int isspace(int c);
int isalpha(int c);
int isalnum(int c);

// ---- output ----
int printf(const char* fmt, ...);
int dprintf(int fd, const char* fmt, ...);
int vdprintf(int fd, const char* fmt, va_list ap);
int snprintf(char* buf, size_t size, const char* fmt, ...);
int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap);
int puts(const char* s);                 // adds a newline, like C
int fputs_fd(const char* s, int fd);     // no newline
void perror(const char* what);
