// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// libk: a deliberately small C library for ring 3 Kunix programs. It is a
// stopgap until a real libc (mlibc, newlib, ...) is ported; the interfaces
// follow POSIX so programs written against it port forward unchanged.

#include "../kunix.h"

int errno;
char** environ;

long syscall3(long n, long a, long b, long c) {
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(n), "D"(a), "S"(b), "d"(c)
                     : "memory");
    return ret;
}

// Raw result -> POSIX result: -1 with errno set on failure.
static long check(long r) {
    if (r < 0 && r > -4096) {
        errno = (int)-r;
        return -1;
    }
    return r;
}

// ---------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------

int main(int argc, char** argv, char** envp);
void __libk_sigrestorer(void);

__attribute__((noreturn))
void __libk_start(int argc, char** argv, char** envp) {
    // environ must be heap-owned so setenv can grow it; start by pointing at
    // the kernel-provided block, and copy on first modification.
    environ = envp;
    exit(main(argc, argv, envp));
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

int open(const char* path, int flags, ...) {
    return (int)check(syscall3(SYS_OPEN, (long)path, flags, 0));
}
int close(int fd)                          { return (int)check(syscall3(SYS_CLOSE, fd, 0, 0)); }
ssize_t read(int fd, void* buf, size_t n)  { return check(syscall3(SYS_READ, fd, (long)buf, (long)n)); }
ssize_t write(int fd, const void* b, size_t n) { return check(syscall3(SYS_WRITE, fd, (long)b, (long)n)); }
off_t lseek(int fd, off_t off, int whence) { return check(syscall3(SYS_LSEEK, fd, off, whence)); }
int fstat(int fd, struct stat* st)         { return (int)check(syscall3(SYS_FSTAT, fd, (long)st, 0)); }
int stat(const char* p, struct stat* st)   { return (int)check(syscall3(SYS_STAT, (long)p, (long)st, 0)); }
int dup(int fd)                            { return (int)check(syscall3(SYS_DUP, fd, 0, 0)); }
int dup2(int a, int b)                     { return (int)check(syscall3(SYS_DUP2, a, b, 0)); }
int pipe(int fds[2])                       { return (int)check(syscall3(SYS_PIPE, (long)fds, 0, 0)); }
int mkdir(const char* p, int mode)         { (void)mode; return (int)check(syscall3(SYS_MKDIR, (long)p, 0, 0)); }
int unlink(const char* p)                  { return (int)check(syscall3(SYS_UNLINK, (long)p, 0, 0)); }
int rmdir(const char* p)                   { return (int)check(syscall3(SYS_RMDIR, (long)p, 0, 0)); }
int rename(const char* a, const char* b)   { return (int)check(syscall3(SYS_RENAME, (long)a, (long)b, 0)); }
int ftruncate(int fd, off_t len)           { return (int)check(syscall3(SYS_FTRUNCATE, fd, len, 0)); }
int chdir(const char* p)                   { return (int)check(syscall3(SYS_CHDIR, (long)p, 0, 0)); }
int readdir_fd(int fd, struct kdirent* e)  { return (int)check(syscall3(SYS_READDIR, fd, (long)e, 0)); }
int ioctl(int fd, unsigned long r, unsigned long a) { return (int)check(syscall3(SYS_IOCTL, fd, (long)r, (long)a)); }

char* getcwd(char* buf, size_t len) {
    return check(syscall3(SYS_GETCWD, (long)buf, (long)len, 0)) < 0 ? NULL : buf;
}

int isatty(int fd) {
    return syscall3(SYS_IOCTL, fd, TIOCGMODE, 0) >= 0;
}

// ---------------------------------------------------------------------------
// Processes
// ---------------------------------------------------------------------------

void _exit(int status) {
    syscall3(SYS_EXIT, status, 0, 0);
    __builtin_unreachable();
}

void exit(int status) {
    _exit(status);
}

pid_t fork(void)                       { return (pid_t)check(syscall3(SYS_FORK, 0, 0, 0)); }
pid_t getpid(void)                     { return (pid_t)syscall3(SYS_GETPID, 0, 0, 0); }
pid_t getppid(void)                    { return (pid_t)syscall3(SYS_GETPPID, 0, 0, 0); }
int setpgid(pid_t pid, pid_t pgid)     { return (int)check(syscall3(SYS_SETPGID, pid, pgid, 0)); }
pid_t getpgid(pid_t pid)               { return (pid_t)check(syscall3(SYS_GETPGID, pid, 0, 0)); }
int tcsetpgrp(int fd, pid_t pgid)      { return (int)check(syscall3(SYS_TCSETPGRP, fd, pgid, 0)); }
pid_t tcgetpgrp(int fd)                { return (pid_t)check(syscall3(SYS_TCGETPGRP, fd, 0, 0)); }
void sched_yield(void)                 { syscall3(SYS_YIELD, 0, 0, 0); }
unsigned long uptime_ms(void)          { return (unsigned long)syscall3(SYS_UPTIME, 0, 0, 0); }
long getcpl(void)                      { return syscall3(SYS_GETCPL, 0, 0, 0); }
int procinfo(unsigned long i, procinfo_t* p) { return (int)check(syscall3(SYS_PROCINFO, (long)i, (long)p, 0)); }
int uname(struct utsname* u)           { return (int)check(syscall3(SYS_UNAME, (long)u, 0, 0)); }
int meminfo(struct meminfo* m)         { return (int)check(syscall3(SYS_MEMINFO, (long)m, 0, 0)); }

time_t time(time_t* out) {
    time_t t = syscall3(SYS_TIME, 0, 0, 0);
    if (out) *out = t;
    return t;
}

// Civil-from-days (Howard Hinnant): no tables, no loops over years.
struct tm* gmtime_r(const time_t* t, struct tm* out) {
    long days = *t / 86400, secs = *t % 86400;
    if (secs < 0) { secs += 86400; days--; }
    out->tm_hour = (int)(secs / 3600);
    out->tm_min = (int)(secs / 60 % 60);
    out->tm_sec = (int)(secs % 60);
    out->tm_wday = (int)((days % 7 + 11) % 7);       // 1970-01-01 was a Thursday
    long z = days + 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned long doe = (unsigned long)(z - era * 146097);
    unsigned long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = (long)yoe + era * 400;
    unsigned long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned long mp = (5 * doy + 2) / 153;
    unsigned long d = doy - (153 * mp + 2) / 5 + 1;
    unsigned long m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
    out->tm_mday = (int)d;
    out->tm_mon = (int)m - 1;
    out->tm_year = (int)(y - 1900);
    // Day of the year, counting from March-based doy back to January.
    int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    out->tm_yday = (int)(mp >= 10 ? doy - 306 : doy + 59 + leap);
    return out;
}

int execve(const char* path, char* const argv[], char* const envp[]) {
    return (int)check(syscall3(SYS_EXECVE, (long)path, (long)argv, (long)envp));
}

pid_t spawn(const char* path, char* const argv[], char* const envp[]) {
    return (pid_t)check(syscall3(SYS_SPAWN, (long)path, (long)argv, (long)envp));
}

pid_t waitpid(pid_t pid, int* status, int options) {
    return (pid_t)check(syscall3(SYS_WAITPID, pid, (long)status, options));
}

pid_t wait(int* status) {
    return waitpid(-1, status, 0);
}

void* sbrk(long increment) {
    long r = syscall3(SYS_SBRK, increment, 0, 0);
    if (r < 0) {
        errno = (int)-r;
        return (void*)-1;
    }
    return (void*)r;
}

int msleep(unsigned long ms) {
    return (int)check(syscall3(SYS_SLEEP, (long)ms, 0, 0));
}

unsigned int sleep(unsigned int seconds) {
    msleep((unsigned long)seconds * 1000);
    return 0;
}

// Try `file` as given if it contains a slash; otherwise each $PATH entry,
// as is and with ".elf" appended (FAT 8.3 names need the extension).
int execvp(const char* file, char* const argv[]) {
    if (strchr(file, '/')) return execve(file, argv, environ);

    const char* path = getenv("PATH");
    if (!path) path = "/bin";
    char buf[256];
    int saved = ENOENT;
    while (*path) {
        const char* colon = strchr(path, ':');
        size_t n = colon ? (size_t)(colon - path) : strlen(path);
        for (int with_ext = 0; with_ext < 2; with_ext++) {
            if (n + strlen(file) + 6 >= sizeof(buf)) continue;
            memcpy(buf, path, n);
            buf[n] = '/';
            strcpy(buf + n + 1, file);
            if (with_ext) strcat(buf, ".elf");
            execve(buf, argv, environ);
            if (errno != ENOENT) saved = errno;   // found but would not run
        }
        path += n;
        if (*path == ':') path++;
    }
    errno = saved;
    return -1;
}

// ---------------------------------------------------------------------------
// Signals
// ---------------------------------------------------------------------------

sighandler_t signal(int sig, sighandler_t handler) {
    long r = syscall3(SYS_SIGACTION, sig, (long)handler, (long)__libk_sigrestorer);
    if (r < 0) {
        errno = (int)-r;
        return SIG_ERR;
    }
    return (sighandler_t)r;
}

int kill(pid_t pid, int sig)   { return (int)check(syscall3(SYS_KILL, pid, sig, 0)); }
int raise(int sig)             { return kill(getpid(), sig); }
long sigmask(int how, unsigned long set) { return check(syscall3(SYS_SIGPROCMASK, how, (long)set, 0)); }

const char* signame(int sig) {
    switch (sig) {
        case SIGHUP:  return "hangup";
        case SIGINT:  return "interrupt";
        case SIGQUIT: return "quit";
        case SIGILL:  return "illegal instruction";
        case SIGTRAP: return "trace trap";
        case SIGABRT: return "abort";
        case SIGBUS:  return "bus error";
        case SIGFPE:  return "floating point exception";
        case SIGKILL: return "killed";
        case SIGSEGV: return "segmentation fault";
        case SIGPIPE: return "broken pipe";
        case SIGALRM: return "alarm";
        case SIGTERM: return "terminated";
        case SIGSTOP: return "stopped (signal)";
        case SIGTSTP: return "suspended";
        default:      return "signal";
    }
}

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

const char* strerror(int e) {
    switch (e) {
        case 0:            return "success";
        case EPERM:        return "operation not permitted";
        case ENOENT:       return "no such file or directory";
        case ESRCH:        return "no such process";
        case EINTR:        return "interrupted system call";
        case EIO:          return "input/output error";
        case ENXIO:        return "no such device or address";
        case E2BIG:        return "argument list too long";
        case ENOEXEC:      return "exec format error";
        case EBADF:        return "bad file descriptor";
        case ECHILD:       return "no child processes";
        case EAGAIN:       return "resource temporarily unavailable";
        case ENOMEM:       return "out of memory";
        case EACCES:       return "permission denied";
        case EFAULT:       return "bad address";
        case EBUSY:        return "device or resource busy";
        case EEXIST:       return "file exists";
        case EXDEV:        return "cross-device link";
        case ENODEV:       return "no such device";
        case ENOTDIR:      return "not a directory";
        case EISDIR:       return "is a directory";
        case EINVAL:       return "invalid argument";
        case EMFILE:       return "too many open files";
        case ENOTTY:       return "not a terminal";
        case ENOSPC:       return "no space left on device";
        case ESPIPE:       return "illegal seek";
        case EROFS:        return "read-only file system";
        case EPIPE:        return "broken pipe";
        case ERANGE:       return "result out of range";
        case ENAMETOOLONG: return "file name too long";
        case ENOSYS:       return "function not implemented";
        case ENOTEMPTY:    return "directory not empty";
        default:           return "unknown error";
    }
}

void perror(const char* what) {
    if (what && *what) dprintf(STDERR_FILENO, "%s: %s\n", what, strerror(errno));
    else dprintf(STDERR_FILENO, "%s\n", strerror(errno));
}

// ---------------------------------------------------------------------------
// Environment
// ---------------------------------------------------------------------------

static int env_owned = 0;            // environ is malloc'd (after setenv)

char* getenv(const char* name) {
    if (!environ) return NULL;
    size_t n = strlen(name);
    for (char** e = environ; *e; e++) {
        if (strncmp(*e, name, n) == 0 && (*e)[n] == '=') return *e + n + 1;
    }
    return NULL;
}

static int env_count(void) {
    int n = 0;
    if (environ) while (environ[n]) n++;
    return n;
}

int unsetenv(const char* name) {
    if (!environ) return 0;
    size_t n = strlen(name);
    for (int i = 0; environ[i]; i++) {
        if (strncmp(environ[i], name, n) == 0 && environ[i][n] == '=') {
            for (int j = i; environ[j]; j++) environ[j] = environ[j + 1];
            i--;
        }
    }
    return 0;
}

int setenv(const char* name, const char* value, int overwrite) {
    if (!name || !*name || strchr(name, '=')) { errno = EINVAL; return -1; }
    if (getenv(name) && !overwrite) return 0;
    unsetenv(name);

    size_t len = strlen(name) + strlen(value) + 2;
    char* entry = malloc(len);
    if (!entry) { errno = ENOMEM; return -1; }
    snprintf(entry, len, "%s=%s", name, value);

    int count = env_count();
    char** fresh = malloc(sizeof(char*) * (size_t)(count + 2));
    if (!fresh) { free(entry); errno = ENOMEM; return -1; }
    for (int i = 0; i < count; i++) fresh[i] = environ[i];
    fresh[count] = entry;
    fresh[count + 1] = NULL;
    if (env_owned) free(environ);
    environ = fresh;
    env_owned = 1;
    return 0;
}

// ---------------------------------------------------------------------------
// Memory: first-fit free list on top of sbrk, with coalescing on free.
// ---------------------------------------------------------------------------

typedef struct block {
    size_t size;                     // payload bytes
    struct block* next;              // next block in address order
    int free;
    int pad;
} block_t;

static block_t* heap_head;
static block_t* heap_tail;

void* malloc(size_t size) {
    if (size == 0) size = 1;
    size = (size + 15) & ~(size_t)15;

    for (block_t* b = heap_head; b; b = b->next) {
        if (!b->free || b->size < size) continue;
        if (b->size >= size + sizeof(block_t) + 16) {   // split
            block_t* rest = (block_t*)((char*)(b + 1) + size);
            rest->size = b->size - size - sizeof(block_t);
            rest->next = b->next;
            rest->free = 1;
            b->next = rest;
            b->size = size;
            if (heap_tail == b) heap_tail = rest;
        }
        b->free = 0;
        return b + 1;
    }

    block_t* b = sbrk((long)(sizeof(block_t) + size));
    if (b == (void*)-1) return NULL;
    b->size = size;
    b->next = NULL;
    b->free = 0;
    if (heap_tail) heap_tail->next = b;
    else heap_head = b;
    heap_tail = b;
    return b + 1;
}

void free(void* p) {
    if (!p) return;
    block_t* b = (block_t*)p - 1;
    b->free = 1;
    // Merge with following free blocks (they are adjacent: one sbrk arena).
    while (b->next && b->next->free) {
        if (b->next == heap_tail) heap_tail = b;
        b->size += sizeof(block_t) + b->next->size;
        b->next = b->next->next;
    }
}

void* calloc(size_t n, size_t size) {
    if (size && n > (size_t)-1 / size) return NULL;
    void* p = malloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}

void* realloc(void* p, size_t size) {
    if (!p) return malloc(size);
    block_t* b = (block_t*)p - 1;
    if (b->size >= size) return p;
    void* q = malloc(size);
    if (!q) return NULL;
    memcpy(q, p, b->size);
    free(p);
    return q;
}

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, size_t n) {
    for (; n; n--, a++, b++) {
        if (*a != *b || !*a) return (unsigned char)*a - (unsigned char)*b;
    }
    return 0;
}

char* strcpy(char* d, const char* s) {
    char* r = d;
    while ((*d++ = *s++)) { }
    return r;
}

char* strncpy(char* d, const char* s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = '\0';
    return d;
}

char* strcat(char* d, const char* s) {
    strcpy(d + strlen(d), s);
    return d;
}

char* strchr(const char* s, int c) {
    for (; *s; s++) if (*s == (char)c) return (char*)s;
    return c == 0 ? (char*)s : NULL;
}

char* strrchr(const char* s, int c) {
    const char* last = NULL;
    for (; *s; s++) if (*s == (char)c) last = s;
    return c == 0 ? (char*)s : (char*)last;
}

char* strstr(const char* hay, const char* needle) {
    size_t n = strlen(needle);
    if (!n) return (char*)hay;
    for (; *hay; hay++) if (strncmp(hay, needle, n) == 0) return (char*)hay;
    return NULL;
}

char* strdup(const char* s) {
    size_t n = strlen(s) + 1;
    char* d = malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

void* memcpy(void* d, const void* s, size_t n) {
    char* dp = d;
    const char* sp = s;
    while (n--) *dp++ = *sp++;
    return d;
}

void* memmove(void* d, const void* s, size_t n) {
    char* dp = d;
    const char* sp = s;
    if (dp < sp) { while (n--) *dp++ = *sp++; }
    else { dp += n; sp += n; while (n--) *--dp = *--sp; }
    return d;
}

void* memset(void* d, int c, size_t n) {
    unsigned char* p = d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
    return 0;
}

int isdigit(int c) { return c >= '0' && c <= '9'; }
int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }

long strtol(const char* s, char** end, int base) {
    while (isspace(*s)) s++;
    int neg = 0;
    if (*s == '-' || *s == '+') neg = *s++ == '-';
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        base = 16;
    } else if (base == 0) {
        base = s[0] == '0' ? 8 : 10;
    }
    long v = 0;
    for (;; s++) {
        int d;
        if (isdigit(*s)) d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
    }
    if (end) *end = (char*)s;
    return neg ? -v : v;
}

int atoi(const char* s) {
    return (int)strtol(s, NULL, 10);
}

// ---------------------------------------------------------------------------
// Formatted output: %d %i %u %x %X %p %s %c %%, with l/ll, width, '-' and '0'.
// ---------------------------------------------------------------------------

typedef struct {
    char* buf;
    size_t cap;
    size_t len;          // characters produced, even past cap
} sink_t;

static void put(sink_t* s, char c) {
    if (s->len + 1 < s->cap) s->buf[s->len] = c;
    s->len++;
}

static void put_padded(sink_t* s, const char* str, size_t n, int width, int left, char pad) {
    int fill = width > (int)n ? width - (int)n : 0;
    if (!left) while (fill-- > 0) put(s, pad);
    for (size_t i = 0; i < n; i++) put(s, str[i]);
    if (left) while (fill-- > 0) put(s, ' ');
}

int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap) {
    sink_t s = { buf, size, 0 };
    for (; *fmt; fmt++) {
        if (*fmt != '%') { put(&s, *fmt); continue; }
        fmt++;
        int left = 0;
        char pad = ' ';
        for (;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') pad = '0';
            else break;
        }
        int width = 0;
        while (isdigit(*fmt)) width = width * 10 + (*fmt++ - '0');
        int lng = 0;
        while (*fmt == 'l') { lng = 1; fmt++; }
        if (*fmt == 'z') { lng = 1; fmt++; }

        char tmp[24];
        int n = 0;
        switch (*fmt) {
            case 'd': case 'i': {
                long v = lng ? va_arg(ap, long) : va_arg(ap, int);
                unsigned long u = v < 0 ? (unsigned long)-v : (unsigned long)v;
                do { tmp[n++] = (char)('0' + u % 10); u /= 10; } while (u);
                if (v < 0) tmp[n++] = '-';
                break;
            }
            case 'u': case 'x': case 'X': case 'p': {
                unsigned long u = (*fmt == 'p' || lng) ? va_arg(ap, unsigned long)
                                                       : va_arg(ap, unsigned int);
                unsigned base = *fmt == 'u' ? 10 : 16;
                const char* digits = *fmt == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
                do { tmp[n++] = digits[u % base]; u /= base; } while (u);
                if (*fmt == 'p') { tmp[n++] = 'x'; tmp[n++] = '0'; }
                break;
            }
            case 'c':
                tmp[n++] = (char)va_arg(ap, int);
                break;
            case 's': {
                const char* str = va_arg(ap, const char*);
                if (!str) str = "(null)";
                put_padded(&s, str, strlen(str), width, left, ' ');
                continue;
            }
            case '%':
                put(&s, '%');
                continue;
            case '\0':
                fmt--;
                continue;
            default:
                put(&s, '%');
                put(&s, *fmt);
                continue;
        }
        // tmp holds the digits backwards.
        char out[24];
        for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
        if (pad == '0' && !left && out[0] == '-') {
            put(&s, '-');
            put_padded(&s, out + 1, (size_t)n - 1, width - 1, 0, '0');
        } else {
            put_padded(&s, out, (size_t)n, width, left, left ? ' ' : pad);
        }
    }
    if (size) s.buf[s.len < size ? s.len : size - 1] = '\0';
    return (int)s.len;
}

int snprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int vdprintf(int fd, const char* fmt, va_list ap) {
    char buf[1024];
    va_list copy;
    va_copy(copy, ap);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if ((size_t)n < sizeof(buf)) {
        write(fd, buf, (size_t)n);
    } else {
        char* big = malloc((size_t)n + 1);
        if (big) {
            vsnprintf(big, (size_t)n + 1, fmt, copy);
            write(fd, big, (size_t)n);
            free(big);
        } else {
            write(fd, buf, sizeof(buf) - 1);
        }
    }
    va_end(copy);
    return n;
}

int dprintf(int fd, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vdprintf(fd, fmt, ap);
    va_end(ap);
    return n;
}

int printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vdprintf(STDOUT_FILENO, fmt, ap);
    va_end(ap);
    return n;
}

int fputs_fd(const char* s, int fd) {
    return (int)write(fd, s, strlen(s));
}

int puts(const char* s) {
    fputs_fd(s, STDOUT_FILENO);
    return (int)write(STDOUT_FILENO, "\n", 1);
}
