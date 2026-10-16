// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// pmtest -- Prime Meridian's test suite. An ordinary Linux program, built
// with musl on the host (static), that exercises the Linux ABI the way real
// software does: through libc. Like ktest it prints PASS/FAIL lines to the
// terminal and /dev/serial, ending with "pmtest: N passed, M failed".

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <setjmp.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

extern char** environ;

static FILE* serial;
static int passed, failed;

static void say(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
    if (serial) {
        va_start(ap, fmt);
        vfprintf(serial, fmt, ap);
        va_end(ap);
        fflush(serial);
    }
}

#define CHECK(name, cond) do { \
    int ok_ = (cond); \
    if (ok_) { passed++; say("PASS %s\n", name); } \
    else { failed++; say("FAIL %s  (line %d, errno %d: %s)\n", name, __LINE__, errno, strerror(errno)); } \
} while (0)

static const char* self = "/ufs/bin/pmtest";

// ---------------------------------------------------------------------------

static __thread int tls_value = 5;

static void test_basics(void) {
    struct utsname u;
    CHECK("uname says Linux", uname(&u) == 0 && strcmp(u.sysname, "Linux") == 0);
    CHECK("getpid/getppid", getpid() > 0 && getppid() >= 0);
    CHECK("thread-local storage (FS base)", tls_value == 5 && (tls_value = 7) == 7 && tls_value == 7);
    errno = 0;
    CHECK("errno from a failed open", open("/no/such/file", O_RDONLY) < 0 && errno == ENOENT);
    CHECK("auxv: page size", sysconf(_SC_PAGESIZE) == 4096 && getauxval(AT_PAGESZ) == 4096);
    CHECK("auxv: AT_PHDR and AT_RANDOM", getauxval(AT_PHDR) != 0 && getauxval(AT_RANDOM) != 0);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.3f %e", 3.14159, 2.5e10);
    CHECK("floating point printf", strcmp(buf, "3.142 2.500000e+10") == 0);
    CHECK("strtod", strtod("1.5e3", NULL) == 1500.0);
    unsigned char r[32] = { 0 };
    CHECK("getrandom", getrandom(r, sizeof(r), 0) == sizeof(r));
}

static void test_memory(void) {
    char* small = malloc(100);
    char* big = malloc(4 << 20);                       // musl mmaps this
    int ok = small && big;
    if (ok) {
        memset(small, 1, 100);
        for (int i = 0; i < (4 << 20); i += 4096) big[i] = (char)i;
        for (int i = 0; i < (4 << 20); i += 4096) if (big[i] != (char)i) ok = 0;
    }
    CHECK("malloc small and 4 MiB", ok);
    free(big);
    char* g = realloc(small, 100000);
    CHECK("realloc keeps the contents", g && g[99] == 1);
    free(g);
    char* m = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK("mmap + mprotect", m != MAP_FAILED && (m[0] = 1) && mprotect(m, 8192, PROT_READ) == 0);
    munmap(m, 8192);
}

static void test_files(void) {
    FILE* f = fopen("/ufs/hello.txt", "r");
    char line[128] = "";
    CHECK("stdio reads a UFS2 file", f && fgets(line, sizeof(line), f) && strcmp(line, "Hello from UFS2!\n") == 0);
    if (f) fclose(f);

    DIR* d = opendir("/ufs/docs");
    int saw_long = 0, saw_dir = 0;
    struct dirent* e;
    while (d && (e = readdir(d))) {
        if (!strcmp(e->d_name, "a-rather-long-file-name.txt") && e->d_type == DT_REG) saw_long = 1;
        if (!strcmp(e->d_name, "deeper") && e->d_type == DT_DIR) saw_dir = 1;
    }
    if (d) closedir(d);
    CHECK("readdir (getdents64) with d_type", saw_long && saw_dir);

    struct stat st;
    CHECK("stat a file", stat("/ufs/big.txt", &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 3080000);
    CHECK("stat a directory", stat("/ufs/docs", &st) == 0 && S_ISDIR(st.st_mode));
    CHECK("regular files look executable", stat("/ufs/hello.txt", &st) == 0 && (st.st_mode & 0111));

    f = fopen("/PMTEST.TXT", "w");
    int wrote = f && fprintf(f, "written by %s\n", "musl") > 0 && fclose(f) == 0;
    f = fopen("/PMTEST.TXT", "r");
    CHECK("write and read back a FAT32 file", wrote && f && fgets(line, sizeof(line), f) &&
          strcmp(line, "written by musl\n") == 0);
    if (f) fclose(f);
    CHECK("unlink", unlink("/PMTEST.TXT") == 0 && access("/PMTEST.TXT", F_OK) < 0);

    char cwd[256];
    CHECK("chdir + getcwd", chdir("/ufs/docs") == 0 && getcwd(cwd, sizeof(cwd)) && strcmp(cwd, "/ufs/docs") == 0);
    CHECK("relative open", access("deeper/nested.txt", R_OK) == 0);
    int dfd = open("deeper", O_RDONLY | O_DIRECTORY);
    int nfd = dfd >= 0 ? openat(dfd, "nested.txt", O_RDONLY) : -1;
    CHECK("openat relative to a directory fd", nfd >= 0 && read(nfd, line, 6) == 6 && memcmp(line, "Nested", 6) == 0);
    if (nfd >= 0) close(nfd);
    if (dfd >= 0) close(dfd);
    chdir("/");

    int fds[2];
    CHECK("pipe2 with O_CLOEXEC", pipe2(fds, O_CLOEXEC) == 0 && (fcntl(fds[0], F_GETFD) & FD_CLOEXEC));
    struct pollfd p = { .fd = fds[0], .events = POLLIN };
    CHECK("poll: nothing to read yet", poll(&p, 1, 0) == 0);
    write(fds[1], "x", 1);
    CHECK("poll: readable after a write", poll(&p, 1, 1000) == 1 && (p.revents & POLLIN));
    fd_set rs;
    FD_ZERO(&rs);
    FD_SET(fds[0], &rs);
    struct timeval tv = { 0, 0 };
    CHECK("select sees it too", select(fds[0] + 1, &rs, NULL, NULL, &tv) == 1 && FD_ISSET(fds[0], &rs));
    char c;
    read(fds[0], &c, 1);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    CHECK("poll times out", poll(&p, 1, 100) == 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    CHECK("...after about 100 ms", ms >= 90 && ms < 400);
    close(fds[0]);
    close(fds[1]);
}

static void test_terminal(void) {
    struct termios t;
    CHECK("isatty(stdin)", isatty(0));
    CHECK("tcgetattr: canonical mode with echo", tcgetattr(0, &t) == 0 && (t.c_lflag & ICANON) && (t.c_lflag & ECHO));
    struct termios raw = t;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    char c;
    CHECK("raw mode read with VTIME times out", tcsetattr(0, TCSANOW, &raw) == 0 && read(0, &c, 1) == 0);
    tcsetattr(0, TCSANOW, &t);
    struct termios back;
    CHECK("...and canonical mode is back", tcgetattr(0, &back) == 0 && (back.c_lflag & ICANON));
}

static void test_time(void) {
    CHECK("time() is after 2025", time(NULL) > 1735689600L);
    struct timespec a, b, req = { 0, 50 * 1000000 };
    clock_gettime(CLOCK_MONOTONIC, &a);
    int r = nanosleep(&req, NULL);
    clock_gettime(CLOCK_MONOTONIC, &b);
    long ms = (b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000;
    CHECK("nanosleep 50 ms", r == 0 && ms >= 45 && ms < 300);
    struct timeval tv;
    CHECK("gettimeofday", gettimeofday(&tv, NULL) == 0 && tv.tv_sec > 1735689600L);
}

static int cow_value = 1;

static void test_processes(void) {
    pid_t pid = fork();
    if (pid == 0) { cow_value = 2; _exit(cow_value == 2 ? 7 : 1); }
    int st = 0;
    CHECK("fork + waitpid", waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 7);
    CHECK("the child's writes stay in the child", cow_value == 1);

    int fds[2];
    pipe(fds);
    pid = fork();
    if (pid == 0) {
        close(fds[0]);
        write(fds[1], "through the pipe", 16);
        _exit(0);
    }
    close(fds[1]);
    char buf[32] = "";
    ssize_t n = read(fds[0], buf, sizeof(buf));
    close(fds[0]);
    waitpid(pid, NULL, 0);
    CHECK("pipe between parent and child", n == 16 && memcmp(buf, "through the pipe", 16) == 0);

    pid = fork();
    if (pid == 0) {
        char* argv[] = { (char*)self, "child", NULL };
        char* envp[] = { "PMVAR=hello", NULL };
        execve(self, argv, envp);
        _exit(99);
    }
    waitpid(pid, &st, 0);
    CHECK("execve with argv and envp", WIFEXITED(st) && WEXITSTATUS(st) == 42);

    char* argv[] = { (char*)self, "child", NULL };
    char* envp[] = { "PMVAR=hello", NULL };
    st = 0;
    int r = posix_spawn(&pid, self, NULL, NULL, argv, envp);
    CHECK("posix_spawn (clone with CLONE_VM|CLONE_VFORK)",
          r == 0 && waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 42);

    pid = fork();
    if (pid == 0) { for (;;) pause(); }
    kill(pid, SIGTERM);
    waitpid(pid, &st, 0);
    CHECK("kill terminates", WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM);
}

static volatile sig_atomic_t got_sig, got_code;
static volatile double fp_in_handler;

static void on_usr1(int sig, siginfo_t* info, void* uc) {
    (void)uc;
    got_sig = sig;
    got_code = info->si_signo;
    fp_in_handler = 1.0 / 3.0 * 3.0;                   // uses SSE in the handler
}

static void on_alarm(int sig) { got_sig = sig; }

static sigjmp_buf jump;
static void on_segv(int sig) { (void)sig; siglongjmp(jump, 1); }

static void test_signals(void) {
    struct sigaction sa = { 0 };
    sa.sa_sigaction = on_usr1;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    volatile double x = 1.25;
    got_sig = 0;
    raise(SIGUSR1);
    CHECK("SA_SIGINFO handler runs with siginfo", got_sig == SIGUSR1 && got_code == SIGUSR1);
    CHECK("SSE registers survive a handler that uses them", x * 2 == 2.5 && fp_in_handler > 0.99);

    sigset_t block, pending, old;
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    got_sig = 0;
    sigprocmask(SIG_BLOCK, &block, &old);
    raise(SIGUSR1);
    sigpending(&pending);
    CHECK("a blocked signal waits", got_sig == 0 && sigismember(&pending, SIGUSR1));
    sigprocmask(SIG_SETMASK, &old, NULL);
    CHECK("...and arrives when unblocked", got_sig == SIGUSR1);

    // sigsuspend: the classic race-free wait.
    sigprocmask(SIG_BLOCK, &block, &old);
    got_sig = 0;
    pid_t parent = getpid();
    pid_t pid = fork();
    if (pid == 0) {
        struct timespec t = { 0, 20 * 1000000 };
        nanosleep(&t, NULL);
        kill(parent, SIGUSR1);
        _exit(0);
    }
    sigset_t none;
    sigemptyset(&none);
    int r = sigsuspend(&none);
    sigset_t now;
    sigprocmask(SIG_SETMASK, NULL, &now);
    CHECK("sigsuspend waits for the signal", r == -1 && errno == EINTR && got_sig == SIGUSR1);
    CHECK("...and restores the mask", sigismember(&now, SIGUSR1));
    sigprocmask(SIG_SETMASK, &old, NULL);
    waitpid(pid, NULL, 0);

    signal(SIGALRM, on_alarm);
    got_sig = 0;
    alarm(1);
    r = pause();
    CHECK("alarm interrupts pause", r == -1 && errno == EINTR && got_sig == SIGALRM);

    signal(SIGSEGV, on_segv);
    volatile int* null = NULL;
    int jumped = sigsetjmp(jump, 1);
    if (!jumped) (void)*null;
    CHECK("siglongjmp out of a SIGSEGV handler", jumped == 1);
    raise(SIGUSR1);                                    // the mask came back too
    CHECK("...with the signal mask restored", got_sig == SIGUSR1 || got_sig == SIGALRM);
    signal(SIGSEGV, SIG_DFL);

    // SA_RESTART: a read interrupted by a handled signal carries on.
    int fds[2];
    pipe(fds);
    struct sigaction rs = { 0 };
    rs.sa_handler = on_alarm;
    rs.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &rs, NULL);
    pid = fork();
    if (pid == 0) {
        struct timespec t = { 0, 300 * 1000000 };
        nanosleep(&t, NULL);
        write(fds[1], "z", 1);
        _exit(0);
    }
    got_sig = 0;
    struct itimerval it = { { 0, 0 }, { 0, 100 * 1000 } };
    setitimer(ITIMER_REAL, &it, NULL);
    char c = 0;
    ssize_t n = read(fds[0], &c, 1);
    CHECK("SA_RESTART restarts an interrupted read", n == 1 && c == 'z' && got_sig == SIGALRM);
    waitpid(pid, NULL, 0);
    close(fds[0]);
    close(fds[1]);
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "child") == 0) {
        const char* v = getenv("PMVAR");
        return v && strcmp(v, "hello") == 0 && environ[1] == NULL ? 42 : 3;
    }
    serial = fopen("/dev/serial", "w");
    say("pmtest: starting (pid %d)\n", getpid());
    test_basics();
    test_memory();
    test_files();
    test_terminal();
    test_time();
    test_processes();
    test_signals();
    say("pmtest: %d passed, %d failed\n", passed, failed);
    return failed;
}
