// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// ktest -- the kernel's syscall test suite. Every check prints PASS or FAIL
// to the screen and to /dev/serial, so a headless boot can be graded from
// the serial log. Exit status is the number of failures.

#include "kunix.h"

static int serial = -1;
static int passed, failed;

static void say(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
    write(STDOUT_FILENO, buf, (size_t)n);
    if (serial >= 0) write(serial, buf, (size_t)n);
}

#define CHECK(name, cond) do { \
    if (cond) { passed++; say("PASS %s\n", name); } \
    else { failed++; say("FAIL %s  (line %d, errno %d: %s)\n", name, __LINE__, errno, strerror(errno)); } \
} while (0)

// Run fn in a child and return its wait status.
static int in_child(int (*fn)(void)) {
    pid_t pid = fork();
    if (pid == 0) _exit(fn());
    int st = -1;
    waitpid(pid, &st, 0);
    return st;
}

// ---------------------------------------------------------------------------

static void test_basics(void) {
    CHECK("running in ring 3", getcpl() == 3);
    struct utsname u;
    CHECK("uname", uname(&u) == 0 && strcmp(u.sysname, "Kunix") == 0);
    errno = 0;
    CHECK("EBADF on a closed fd", read(77, NULL, 0) < 0 && errno == EBADF);
    CHECK("EFAULT on a kernel pointer",
          write(STDOUT_FILENO, (void*)0xFFFFFFFF80000000UL, 8) < 0 && errno == EFAULT);
    CHECK("EFAULT on NULL", read(STDIN_FILENO, NULL, 8) < 0 && errno == EFAULT);
    CHECK("ENOENT", open("/no/such/file", O_RDONLY) < 0 && errno == ENOENT);
    CHECK("ENOSYS for an unknown syscall", syscall3(999, 0, 0, 0) == -ENOSYS);
}

static void test_memory(void) {
    unsigned char* p = malloc(1024 * 1024);
    int ok = p != NULL;
    if (ok) {
        for (int i = 0; i < 1024 * 1024; i++) p[i] = (unsigned char)(i * 7);
        for (int i = 0; i < 1024 * 1024; i++) if (p[i] != (unsigned char)(i * 7)) { ok = 0; break; }
        free(p);
    }
    CHECK("malloc 1 MiB and use it", ok);
    char* a = malloc(100);
    free(a);
    char* b = malloc(80);
    CHECK("free'd memory is reused", a == b);
    free(b);
}

static void test_pipes(void) {
    int fds[2];
    CHECK("pipe", pipe(fds) == 0);
    char buf[16] = {0};
    CHECK("pipe write", write(fds[1], "hello", 5) == 5);
    CHECK("pipe read", read(fds[0], buf, sizeof(buf)) == 5 && memcmp(buf, "hello", 5) == 0);
    close(fds[1]);
    CHECK("pipe EOF once writers are gone", read(fds[0], buf, sizeof(buf)) == 0);
    close(fds[0]);

    // dup2 onto a high fd, write through it.
    pipe(fds);
    CHECK("dup2", dup2(fds[1], 20) == 20);
    write(20, "x", 1);
    CHECK("write via the dup'd fd", read(fds[0], buf, 1) == 1 && buf[0] == 'x');
    close(20); close(fds[0]); close(fds[1]);

    // A big transfer that has to block the writer repeatedly.
    pipe(fds);
    pid_t pid = fork();
    if (pid == 0) {
        close(fds[0]);
        char chunk[1000];
        for (int i = 0; i < 100; i++) {
            memset(chunk, 'a' + i % 26, sizeof(chunk));
            write(fds[1], chunk, sizeof(chunk));
        }
        _exit(0);
    }
    close(fds[1]);
    long total = 0, bad = 0;
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++, total++) if (buf[i] != 'a' + (total / 1000) % 26) bad++;
    }
    close(fds[0]);
    waitpid(pid, NULL, 0);
    CHECK("100 KB through a 4 KB pipe, in order", total == 100000 && bad == 0);
}

static int global_value = 1;

static int child_exit7(void) { return 7; }
static int child_touch_global(void) { global_value = 99; return global_value == 99 ? 0 : 1; }

static void test_fork_exec(void) {
    int st = in_child(child_exit7);
    CHECK("fork + exit status", WIFEXITED(st) && WEXITSTATUS(st) == 7);
    st = in_child(child_touch_global);
    CHECK("child writes its own copy of memory", WIFEXITED(st) && WEXITSTATUS(st) == 0 && global_value == 1);

    pid_t me = getpid();
    pid_t pid = fork();
    if (pid == 0) _exit(getppid() == me ? 0 : 1);
    waitpid(pid, &st, 0);
    CHECK("getppid in the child", WEXITSTATUS(st) == 0);

    errno = 0;
    CHECK("ECHILD with no children", waitpid(-1, NULL, 0) < 0 && errno == ECHILD);

    pid = fork();
    if (pid == 0) { msleep(300); _exit(0); }
    CHECK("WNOHANG returns 0 while the child runs", waitpid(pid, NULL, WNOHANG) == 0);
    waitpid(pid, NULL, 0);

    // execve with arguments, output captured through a pipe.
    int fds[2];
    pipe(fds);
    pid = fork();
    if (pid == 0) {
        dup2(fds[1], STDOUT_FILENO);
        close(fds[0]); close(fds[1]);
        char* argv[] = { "echo", "exec", "works", NULL };
        execve("/bin/echo.elf", argv, environ);
        _exit(126);
    }
    close(fds[1]);
    char buf[64] = {0};
    ssize_t got = 0, n;
    while ((n = read(fds[0], buf + got, sizeof(buf) - 1 - (size_t)got)) > 0) got += n;
    close(fds[0]);
    waitpid(pid, &st, 0);
    CHECK("execve passes argv", WIFEXITED(st) && WEXITSTATUS(st) == 0 && strcmp(buf, "exec works\n") == 0);

    pid = fork();
    if (pid == 0) {
        char* argv[] = { "false", NULL };
        execve("/bin/false.elf", argv, environ);
        _exit(126);
    }
    waitpid(pid, &st, 0);
    CHECK("execve'd program's exit status", WIFEXITED(st) && WEXITSTATUS(st) == 1);

    char* argv[] = { "nope", NULL };
    CHECK("failed execve returns to the caller",
          execve("/bin/does-not-exist", argv, environ) < 0 && errno == ENOENT);

    pid = fork();
    if (pid == 0) {
        chdir("/dev");
        _exit(0);
    }
    waitpid(pid, NULL, 0);
    char cwd[64];
    CHECK("working directory is per-process", getcwd(cwd, sizeof(cwd)) && strcmp(cwd, "/") == 0);
}

static volatile int got_usr1;
static void on_usr1(int sig) { got_usr1 = sig; }

__attribute__((noreturn)) static int child_sleep_forever(void) { for (;;) msleep(1000); }

static int child_interrupted_read(void) {
    signal(SIGUSR1, on_usr1);
    int fds[2];
    pipe(fds);                       // nobody will ever write to it
    ssize_t r = read(fds[0], &fds, 1);
    return (r < 0 && errno == EINTR && got_usr1 == SIGUSR1) ? 0 : 1;
}

static int child_sigpipe(void) {
    int fds[2];
    pipe(fds);
    close(fds[0]);
    write(fds[1], "x", 1);           // default SIGPIPE: killed here
    return 0;
}

static void test_signals(void) {
    // A handler runs, and the code around it keeps its registers.
    signal(SIGUSR1, on_usr1);
    volatile long a = 12345, b = 67890;
    got_usr1 = 0;
    raise(SIGUSR1);
    CHECK("handler runs on raise()", got_usr1 == SIGUSR1);
    CHECK("registers survive the handler", a == 12345 && b == 67890);

    signal(SIGUSR1, SIG_IGN);
    got_usr1 = 0;
    raise(SIGUSR1);
    CHECK("SIG_IGN ignores", got_usr1 == 0);
    signal(SIGUSR1, SIG_DFL);

    pid_t pid = fork();
    if (pid == 0) _exit(child_sleep_forever());
    msleep(50);
    kill(pid, SIGTERM);
    int st;
    waitpid(pid, &st, 0);
    CHECK("kill -TERM terminates", WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM);

    pid = fork();
    if (pid == 0) _exit(child_interrupted_read());
    msleep(100);
    kill(pid, SIGUSR1);
    waitpid(pid, &st, 0);
    CHECK("a signal interrupts a blocked read with EINTR", WIFEXITED(st) && WEXITSTATUS(st) == 0);

    st = in_child(child_sigpipe);
    CHECK("writing to a reader-less pipe raises SIGPIPE", WIFSIGNALED(st) && WTERMSIG(st) == SIGPIPE);

    signal(SIGPIPE, SIG_IGN);
    int fds[2];
    pipe(fds);
    close(fds[0]);
    CHECK("...or EPIPE when SIGPIPE is ignored", write(fds[1], "x", 1) < 0 && errno == EPIPE);
    close(fds[1]);
    signal(SIGPIPE, SIG_DFL);

    // Signal a whole process group.
    pid_t kids[2];
    for (int i = 0; i < 2; i++) {
        kids[i] = fork();
        if (kids[i] == 0) { for (;;) msleep(1000); }
    }
    setpgid(kids[0], kids[0]);
    setpgid(kids[1], kids[0]);
    msleep(50);
    kill(-kids[0], SIGKILL);
    int s0, s1;
    waitpid(kids[0], &s0, 0);
    waitpid(kids[1], &s1, 0);
    CHECK("kill(-pgid) reaches the whole group",
          WIFSIGNALED(s0) && WTERMSIG(s0) == SIGKILL && WIFSIGNALED(s1) && WTERMSIG(s1) == SIGKILL);

    pid = fork();
    if (pid == 0) { volatile int* p = 0; _exit(*p); }
    waitpid(pid, &st, 0);
    CHECK("a NULL dereference is SIGSEGV", WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV);
}

// Floating point keeps its state across context switches.
static double fp_work(void) {
    double x = 0.0;
    for (int i = 1; i <= 200000; i++) {
        x += 1.0 / (double)i;
        if (i % 5000 == 0) sched_yield();
    }
    return x;
}

static void test_fpu(void) {
    double expect = fp_work();
    pid_t kids[3];
    for (int i = 0; i < 3; i++) {
        kids[i] = fork();
        if (kids[i] == 0) _exit(fp_work() == expect ? 0 : 1);
    }
    int ok = 1;
    for (int i = 0; i < 3; i++) {
        int st;
        waitpid(kids[i], &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st)) ok = 0;
    }
    CHECK("SSE state survives preemption (3 processes)", ok);
}

static void test_files(void) {
    struct stat st;
    CHECK("stat /", stat("/", &st) == 0 && S_ISDIR(st.st_mode));
    CHECK("stat /dev/null", stat("/dev/null", &st) == 0 && (st.st_mode & S_IFMT) == S_IFCHR);

    int fd = open("/dev/zero", O_RDONLY);
    char z[16];
    memset(z, 1, sizeof(z));
    CHECK("/dev/zero reads zeros", fd >= 0 && read(fd, z, sizeof(z)) == 16 && z[0] == 0 && z[15] == 0);
    close(fd);
    fd = open("/dev/null", O_WRONLY);
    CHECK("/dev/null swallows writes", fd >= 0 && write(fd, "abc", 3) == 3);
    close(fd);

    fd = open("/dev", O_RDONLY | O_DIRECTORY);
    int names = 0;
    struct kdirent e;
    while (readdir_fd(fd, &e) == 1) {
        if (!strcmp(e.d_name, "tty") || !strcmp(e.d_name, "null") || !strcmp(e.d_name, "zero")) names++;
    }
    close(fd);
    CHECK("readdir /dev", names == 3);

    // Read the same file whole and in pieces with lseek.
    fd = open("/README.TXT", O_RDONLY);
    char whole[256] = {0}, part[16] = {0};
    ssize_t n = fd >= 0 ? read(fd, whole, sizeof(whole) - 1) : -1;
    CHECK("fstat size matches read", fd >= 0 && fstat(fd, &st) == 0 && (ssize_t)st.st_size == n);
    CHECK("lseek SEEK_END", lseek(fd, 0, SEEK_END) == n);
    lseek(fd, 10, SEEK_SET);
    CHECK("lseek + read", read(fd, part, 5) == 5 && memcmp(part, whole + 10, 5) == 0);
    close(fd);

    CHECK("O_DIRECTORY on a file fails", open("/README.TXT", O_RDONLY | O_DIRECTORY) < 0 && errno == ENOTDIR);
    CHECK("writing a directory fails", open("/", O_WRONLY) < 0 && errno == EISDIR);
}

static void test_writes(void) {
    const char* path = "/KTEST.TMP";
    unlink(path);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK("create a file", fd >= 0);
    if (fd < 0) return;

    // 10000 bytes: spans several clusters on any sane FAT32 volume.
    static char data[10000];
    for (int i = 0; i < (int)sizeof(data); i++) data[i] = (char)('A' + i % 23);
    CHECK("write 10000 bytes", write(fd, data, sizeof(data)) == (ssize_t)sizeof(data));
    close(fd);

    struct stat st;
    CHECK("its size on disk", stat(path, &st) == 0 && st.st_size == sizeof(data));

    static char back[10000];
    fd = open(path, O_RDONLY);
    ssize_t got = 0, n;
    while ((n = read(fd, back + got, sizeof(back) - (size_t)got)) > 0) got += n;
    close(fd);
    CHECK("read it back byte for byte", got == (ssize_t)sizeof(data) && memcmp(back, data, sizeof(data)) == 0);

    fd = open(path, O_WRONLY | O_APPEND);
    write(fd, "tail", 4);
    close(fd);
    CHECK("O_APPEND", stat(path, &st) == 0 && st.st_size == sizeof(data) + 4);

    fd = open(path, O_RDWR);
    lseek(fd, 5000, SEEK_SET);
    write(fd, "MIDDLE", 6);
    lseek(fd, 4998, SEEK_SET);
    char mid[10] = {0};
    read(fd, mid, 10);
    CHECK("overwrite in the middle", memcmp(mid, data + 4998, 2) == 0 && memcmp(mid + 2, "MIDDLE", 6) == 0 &&
                                    memcmp(mid + 8, data + 5006, 2) == 0);
    CHECK("ftruncate shrinks", ftruncate(fd, 100) == 0 && fstat(fd, &st) == 0 && st.st_size == 100);
    close(fd);

    fd = open(path, O_WRONLY | O_TRUNC);
    close(fd);
    CHECK("O_TRUNC empties", stat(path, &st) == 0 && st.st_size == 0);
    CHECK("O_CREAT|O_EXCL on an existing file", open(path, O_WRONLY | O_CREAT | O_EXCL) < 0 && errno == EEXIST);
    CHECK("unlink", unlink(path) == 0 && stat(path, &st) < 0 && errno == ENOENT);

    rmdir("/KTDIR/INNER.TXT");
    unlink("/KTDIR/INNER.TXT");
    rmdir("/KTDIR");
    CHECK("mkdir", mkdir("/KTDIR", 0755) == 0 && stat("/KTDIR", &st) == 0 && S_ISDIR(st.st_mode));
    fd = open("/KTDIR/INNER.TXT", O_WRONLY | O_CREAT);
    write(fd, "inside", 6);
    close(fd);
    CHECK("a file inside the new directory", stat("/KTDIR/INNER.TXT", &st) == 0 && st.st_size == 6);
    CHECK("rmdir refuses a non-empty directory", rmdir("/KTDIR") < 0 && errno == ENOTEMPTY);
    CHECK("unlink inside it", unlink("/KTDIR/INNER.TXT") == 0);
    CHECK("rmdir an empty directory", rmdir("/KTDIR") == 0 && stat("/KTDIR", &st) < 0);
    CHECK("names longer than 8.3 are refused", open("/a-very-long-name.text", O_WRONLY | O_CREAT) < 0);
    CHECK("UFS2 is read-only", open("/ufs/new.txt", O_WRONLY | O_CREAT) < 0 && errno == EROFS);
}

// Run lots of process and pipe churn; afterwards every physical page and
// every byte of kernel heap must be back where it started.
static void test_leaks(void) {
    struct meminfo before, after;
    // Warm up once so one-time allocations (page tables for new slots, ...)
    // are not counted as leaks.
    for (int round = 0; round < 2; round++) {
        if (round == 1) meminfo(&before);
        for (int i = 0; i < 25; i++) {
            int fds[2];
            pipe(fds);
            pid_t pid = fork();
            if (pid == 0) {
                dup2(fds[1], STDOUT_FILENO);
                close(fds[0]); close(fds[1]);
                char* argv[] = { "true", NULL };
                execve("/bin/true.elf", argv, environ);
                _exit(1);
            }
            close(fds[1]);
            char c;
            while (read(fds[0], &c, 1) > 0) { }
            close(fds[0]);
            waitpid(pid, NULL, 0);
            int fd = open("/README.TXT", O_RDONLY);
            close(fd);
        }
    }
    meminfo(&after);
    CHECK("no physical pages leaked by fork/exec/pipe", after.free_pages == before.free_pages);
    CHECK("no kernel heap leaked", after.heap_used == before.heap_used);
    if (after.free_pages != before.free_pages || after.heap_used != before.heap_used) {
        say("   pages %lu -> %lu, heap %lu -> %lu\n", before.free_pages, after.free_pages,
            before.heap_used, after.heap_used);
    }
}

int main(int argc, char** argv) {
    serial = open("/dev/serial", O_WRONLY);
    int only_files = argc > 1 && strcmp(argv[1], "files") == 0;
    say("ktest: starting (pid %d)\n", getpid());

    if (!only_files) {
        test_basics();
        test_memory();
        test_pipes();
        test_fork_exec();
        test_signals();
        test_fpu();
        test_leaks();
    }
    test_files();
    test_writes();

    say("ktest: %d passed, %d failed\n", passed, failed);
    return failed;
}
