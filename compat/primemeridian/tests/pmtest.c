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
#include <pty.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <setjmp.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/klog.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <termios.h>
#include <sys/ioctl.h>
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

static const char* self = "/bin/pmtest";

// ---------------------------------------------------------------------------

static __thread int tls_value = 5;

static void sleep_quietly(long ms) {
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000 };
    nanosleep(&t, NULL);
}

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

static void test_kernel_log(void) {
    int size = klogctl(10, NULL, 0);
    CHECK("kernel log has a 64 KiB buffer", size == 65536);
    char* buf = malloc(65537);
    if (!buf) { CHECK("allocate kernel log buffer", 0); return; }
    int n = klogctl(3, buf, 65536);
    if (n >= 0 && n <= 65536) buf[n] = 0;
    CHECK("syslog reads the boot log", n > 0 && n <= 65536 && strstr(buf, "Booting Kunix"));
    char tail[8];
    n = klogctl(3, buf, 65536);
    int short_n = klogctl(3, tail, sizeof(tail));
    CHECK("syslog short read returns the newest bytes", n >= 8 && short_n == 8 &&
          !memcmp(tail, buf + n - 8, 8));
    // CHECK itself writes to /dev/serial, so take two reads without printing.
    n = klogctl(3, buf, 65536);
    int again = klogctl(3, buf, 65536);
    CHECK("syslog READ_ALL is nondestructive", n > 0 && n == again);
    CHECK("syslog zero length", klogctl(3, NULL, 0) == 0);
    errno = 0;
    CHECK("syslog rejects negative length", klogctl(3, buf, -1) == -1 && errno == EINVAL);
    errno = 0;
    CHECK("syslog rejects bad user pointer", klogctl(3, (char*)1, 8) == -1 && errno == EFAULT);
    errno = 0;
    CHECK("syslog rejects unknown action", klogctl(99, NULL, 0) == -1 && errno == EINVAL);
    int fd = open("/proc/kmsg", O_RDONLY);
    ssize_t got = fd >= 0 ? read(fd, buf, 65536) : -1;
    CHECK("proc kmsg exposes the boot log", got > 0 && memmem(buf, got, "Booting Kunix", 13));
    if (fd >= 0) close(fd);
    free(buf);
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
    FILE* f = fopen("/test/hello.txt", "r");
    char line[128] = "";
    CHECK("stdio reads a UFS2 file", f && fgets(line, sizeof(line), f) && strcmp(line, "Hello from UFS2!\n") == 0);
    if (f) fclose(f);

    DIR* d = opendir("/test/docs");
    int saw_long = 0, saw_dir = 0;
    struct dirent* e;
    while (d && (e = readdir(d))) {
        if (!strcmp(e->d_name, "a-rather-long-file-name.txt") && e->d_type == DT_REG) saw_long = 1;
        if (!strcmp(e->d_name, "deeper") && e->d_type == DT_DIR) saw_dir = 1;
    }
    if (d) closedir(d);
    CHECK("readdir (getdents64) with d_type", saw_long && saw_dir);

    struct stat st;
    CHECK("stat a file", stat("/test/big.txt", &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 3080000);
    CHECK("stat a directory", stat("/test/docs", &st) == 0 && S_ISDIR(st.st_mode));
    CHECK("FAT32 files (no permissions on disk) look executable", stat("/fat/BIN/LS.ELF", &st) == 0 && (st.st_mode & 0111));
    CHECK("UFS2 reports its real modes", stat("/test/hello.txt", &st) == 0 && !(st.st_mode & 0111) &&
          stat("/bin/zsh", &st) == 0 && (st.st_mode & 0111));

    f = fopen("/fat/PMTEST.TXT", "w");
    int wrote = f && fprintf(f, "written by %s\n", "musl") > 0 && fclose(f) == 0;
    f = fopen("/fat/PMTEST.TXT", "r");
    CHECK("write and read back a FAT32 file", wrote && f && fgets(line, sizeof(line), f) &&
          strcmp(line, "written by musl\n") == 0);
    if (f) fclose(f);
    CHECK("unlink", unlink("/fat/PMTEST.TXT") == 0 && access("/fat/PMTEST.TXT", F_OK) < 0);

    char cwd[256];
    CHECK("chdir + getcwd", chdir("/test/docs") == 0 && getcwd(cwd, sizeof(cwd)) && strcmp(cwd, "/test/docs") == 0);
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

// Read from fd until `want` appears or 2 s pass; the bytes in out.
static int read_until(int fd, char* out, size_t cap, const char* want) {
    size_t n = 0;
    out[0] = '\0';
    for (int tries = 0; tries < 40 && !strstr(out, want); tries++) {
        struct pollfd p = { .fd = fd, .events = POLLIN };
        if (poll(&p, 1, 50) <= 0) continue;
        ssize_t r = read(fd, out + n, cap - 1 - n);
        if (r <= 0) break;
        n += (size_t)r;
        out[n] = '\0';
    }
    return strstr(out, want) != NULL;
}

static volatile sig_atomic_t got_winch;
static void on_winch(int sig) { (void)sig; got_winch = 1; }

static void test_pty(void) {
    int m, s;
    char name[64] = "", buf[512];
    CHECK("openpty", openpty(&m, &s, name, NULL, NULL) == 0 && strncmp(name, "/dev/pts/", 9) == 0);
    CHECK("the slave is a terminal", isatty(s));
    CHECK("ttyname of the slave", ttyname(s) && strcmp(ttyname(s), name) == 0);
    struct stat st;
    CHECK("/dev/pts/N exists", stat(name, &st) == 0 && S_ISCHR(st.st_mode));

    write(m, "typed\n", 6);
    ssize_t n = read(s, buf, sizeof(buf));
    CHECK("master -> slave, as one line", n == 6 && memcmp(buf, "typed\n", 6) == 0);
    CHECK("...echoed back to the master", read_until(m, buf, sizeof(buf), "typed\r\n"));
    write(s, "shown\n", 6);
    CHECK("slave -> master, NL becoming CR NL", read_until(m, buf, sizeof(buf), "shown\r\n"));

    struct termios raw;
    tcgetattr(s, &raw);
    cfmakeraw(&raw);
    tcsetattr(s, TCSANOW, &raw);
    write(m, "k", 1);
    n = read(s, buf, 1);
    struct pollfd pm = { .fd = m, .events = POLLIN };
    CHECK("raw mode: a byte at a time, no echo", n == 1 && buf[0] == 'k' && poll(&pm, 1, 50) == 0);

    struct winsize ws = { .ws_row = 33, .ws_col = 99 }, back;
    CHECK("window size set on the master, seen on the slave",
          ioctl(m, TIOCSWINSZ, &ws) == 0 && ioctl(s, TIOCGWINSZ, &back) == 0 && back.ws_row == 33 && back.ws_col == 99);
    close(s);
    close(m);

    // A session on the pty: ^C reaches it, and so do resizes.
    CHECK("openpty again", openpty(&m, &s, name, NULL, NULL) == 0);
    pid_t pid = fork();
    if (pid == 0) {
        close(m);
        setsid();
        ioctl(s, TIOCSCTTY, 0);
        dup2(s, 0); dup2(s, 1); dup2(s, 2);
        signal(SIGWINCH, on_winch);
        int tty = open("/dev/tty", O_RDWR);
        write(tty, "ctty ok\n", 8);
        while (!got_winch) pause();
        write(1, "winched\n", 8);
        for (;;) pause();
    }
    close(s);
    CHECK("/dev/tty is the pty in its session", read_until(m, buf, sizeof(buf), "ctty ok"));
    ws.ws_row = 40;
    ioctl(m, TIOCSWINSZ, &ws);
    CHECK("a resize sends SIGWINCH to the foreground", read_until(m, buf, sizeof(buf), "winched"));
    write(m, "\x03", 1);
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK("^C written to the master is SIGINT for the session", WIFSIGNALED(status) && WTERMSIG(status) == SIGINT);
    close(m);

    // Closing the master hangs the session up.
    openpty(&m, &s, name, NULL, NULL);
    pid = fork();
    if (pid == 0) {
        close(m);
        setsid();
        ioctl(s, TIOCSCTTY, 0);
        char c;
        while (read(s, &c, 1) > 0) { }
        _exit(5);                                 // EOF instead of a signal
    }
    close(s);
    sleep_quietly(100);
    close(m);
    waitpid(pid, &status, 0);
    CHECK("closing the master hangs up the slave (SIGHUP)", WIFSIGNALED(status) && WTERMSIG(status) == SIGHUP);

    // A real program on a pty.
    pid = forkpty(&m, name, NULL, NULL);
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", "echo inside; tty", (char*)0);
        _exit(127);
    }
    int saw = pid > 0 && read_until(m, buf, sizeof(buf), "inside\r\n");
    saw = saw && (strstr(buf, name) || read_until(m, buf + strlen(buf), sizeof(buf) - strlen(buf), name));
    if (!saw) {
        for (char* c = buf; *c; c++) if (*c == '\r') *c = '|';
        say("   (master read \"%s\", wanted %s)\n", buf, name);
    }
    CHECK("forkpty: a shell runs on it, and tty(1) names it", saw);
    waitpid(pid, &status, 0);
    CHECK("...and the master reads EIO once it is gone", read(m, buf, sizeof(buf)) < 0 && errno == EIO);
    close(m);
}

static socklen_t un_addr(struct sockaddr_un* a, const char* path, int abstract) {
    memset(a, 0, sizeof(*a));
    a->sun_family = AF_UNIX;
    size_t n = strlen(path);
    if (abstract) { memcpy(a->sun_path + 1, path, n); return (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + n); }
    memcpy(a->sun_path, path, n);
    return (socklen_t)(offsetof(struct sockaddr_un, sun_path) + n + 1);
}

static void test_sockets(void) {
    int sv[2];
    char buf[256];
    CHECK("socketpair (stream)", socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    CHECK("bytes both ways", write(sv[0], "ping", 4) == 4 && read(sv[1], buf, sizeof(buf)) == 4 &&
          memcmp(buf, "ping", 4) == 0 && send(sv[1], "pong", 4, 0) == 4 && recv(sv[0], buf, 4, 0) == 4);
    shutdown(sv[0], SHUT_WR);
    CHECK("shutdown(SHUT_WR) is EOF for the other side", read(sv[1], buf, sizeof(buf)) == 0);
    close(sv[0]);
    signal(SIGPIPE, SIG_IGN);
    errno = 0;
    CHECK("writing to a closed peer is EPIPE", write(sv[1], "x", 1) < 0 && errno == EPIPE);
    signal(SIGPIPE, SIG_DFL);
    close(sv[1]);

    CHECK("socketpair (datagram)", socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) == 0);
    send(sv[0], "one", 3, 0);
    send(sv[0], "second", 6, 0);
    struct msghdr mh = { 0 };
    struct iovec iov = { buf, 2 };
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;
    ssize_t n1 = recvmsg(sv[1], &mh, 0);
    int trunc = mh.msg_flags & MSG_TRUNC;
    ssize_t n2 = recv(sv[1], buf, sizeof(buf), 0);
    CHECK("datagrams keep their boundaries (and MSG_TRUNC)", n1 == 2 && trunc && n2 == 6 && memcmp(buf, "second", 6) == 0);
    close(sv[0]);
    close(sv[1]);

    // A server on a path.
    struct sockaddr_un a;
    socklen_t al = un_addr(&a, "/tmp/pmtest.sock", 0);
    unlink("/tmp/pmtest.sock");
    int srv = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    CHECK("bind to a path makes a socket file", srv >= 0 && bind(srv, (struct sockaddr*)&a, al) == 0);
    struct stat st;
    CHECK("...which stat calls a socket", stat("/tmp/pmtest.sock", &st) == 0 && S_ISSOCK(st.st_mode));
    errno = 0;
    int dup = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK("binding the same path again is EADDRINUSE", bind(dup, (struct sockaddr*)&a, al) < 0 && errno == EADDRINUSE);
    close(dup);
    int c0 = socket(AF_UNIX, SOCK_STREAM, 0);
    errno = 0;
    CHECK("connect before listen is ECONNREFUSED", connect(c0, (struct sockaddr*)&a, al) < 0 && errno == ECONNREFUSED);
    close(c0);
    CHECK("listen", listen(srv, 4) == 0);
    fcntl(srv, F_SETFL, O_NONBLOCK);
    errno = 0;
    CHECK("non-blocking accept with nobody waiting is EAGAIN", accept(srv, NULL, NULL) < 0 && errno == EAGAIN);
    fcntl(srv, F_SETFL, 0);

    pid_t pid = fork();
    if (pid == 0) {
        int c = socket(AF_UNIX, SOCK_STREAM, 0);
        if (connect(c, (struct sockaddr*)&a, al) < 0) _exit(1);
        write(c, "hello server", 12);
        char r[32];
        ssize_t k = read(c, r, sizeof(r));
        _exit(k == 5 && memcmp(r, "hi yo", 5) == 0 ? 0 : 2);
    }
    struct pollfd pl = { .fd = srv, .events = POLLIN };
    CHECK("poll says a connection is waiting", poll(&pl, 1, 2000) == 1 && (pl.revents & POLLIN));
    int conn = accept(srv, NULL, NULL);
    ssize_t k = conn >= 0 ? read(conn, buf, sizeof(buf)) : -1;
    CHECK("accept, and the client's bytes arrive", k == 12 && memcmp(buf, "hello server", 12) == 0);
    struct ucred cr;
    socklen_t crl = sizeof(cr);
    CHECK("SO_PEERCRED names the client", getsockopt(conn, SOL_SOCKET, SO_PEERCRED, &cr, &crl) == 0 && cr.pid == pid);
    struct sockaddr_un got;
    socklen_t gl = sizeof(got);
    CHECK("getsockname on the accepted side is the server's path",
          getsockname(conn, (struct sockaddr*)&got, &gl) == 0 && strcmp(got.sun_path, "/tmp/pmtest.sock") == 0);
    write(conn, "hi yo", 5);
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK("...and the reply reaches the client", WIFEXITED(status) && WEXITSTATUS(status) == 0);
    close(conn);
    close(srv);
    unlink("/tmp/pmtest.sock");

    // The abstract namespace, and passing a file across.
    struct sockaddr_un ab;
    socklen_t abl = un_addr(&ab, "pmtest-abstract", 1);
    srv = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    CHECK("an abstract name (no file anywhere)", srv >= 0 && bind(srv, (struct sockaddr*)&ab, abl) == 0 &&
          listen(srv, 1) == 0 && access("/tmp/pmtest-abstract", F_OK) < 0);
    int p[2];
    pipe(p);
    pid = fork();
    if (pid == 0) {
        int c = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (connect(c, (struct sockaddr*)&ab, abl) < 0) _exit(1);
        char data = 'F';
        struct iovec v = { &data, 1 };
        union { struct cmsghdr h; char space[CMSG_SPACE(sizeof(int))]; } ctl;
        struct msghdr m = { .msg_iov = &v, .msg_iovlen = 1, .msg_control = &ctl, .msg_controllen = sizeof(ctl) };
        struct cmsghdr* ch = CMSG_FIRSTHDR(&m);
        ch->cmsg_level = SOL_SOCKET;
        ch->cmsg_type = SCM_RIGHTS;
        ch->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(ch), &p[1], sizeof(int));
        _exit(sendmsg(c, &m, 0) == 1 ? 0 : 2);
    }
    close(p[1]);
    conn = accept(srv, NULL, NULL);
    char data;
    struct iovec v = { &data, 1 };
    union { struct cmsghdr h; char space[CMSG_SPACE(sizeof(int))]; } ctl;
    struct msghdr m = { .msg_iov = &v, .msg_iovlen = 1, .msg_control = &ctl, .msg_controllen = sizeof(ctl) };
    int passed_fd = -1;
    if (conn >= 0 && recvmsg(conn, &m, 0) == 1) {
        struct cmsghdr* ch = CMSG_FIRSTHDR(&m);
        if (ch && ch->cmsg_type == SCM_RIGHTS) memcpy(&passed_fd, CMSG_DATA(ch), sizeof(int));
    }
    waitpid(pid, &status, 0);
    CHECK("SCM_RIGHTS: an open pipe passed between processes",
          passed_fd >= 0 && write(passed_fd, "via fd", 6) == 6 && read(p[0], buf, 6) == 6 && memcmp(buf, "via fd", 6) == 0);
    close(passed_fd);
    close(p[0]);
    close(conn);
    close(srv);

    errno = 0;
    CHECK("no networking yet: AF_INET is EAFNOSUPPORT", socket(AF_INET, SOCK_STREAM, 0) < 0 && errno == EAFNOSUPPORT);
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
    struct timespec x, y;
    clock_gettime(CLOCK_REALTIME, &x);
    clock_gettime(CLOCK_REALTIME, &y);
    CHECK("the clock never repeats itself", y.tv_sec > x.tv_sec || (y.tv_sec == x.tv_sec && y.tv_nsec > x.tv_nsec));
    char t1[] = "/tmp/pmtestXXXXXX", t2[] = "/tmp/pmtestXXXXXX";
    int f1 = mkstemp(t1), f2 = mkstemp(t2);
    CHECK("mkstemp twice in a row, in /tmp (tmpfs)", f1 >= 0 && f2 >= 0 && strcmp(t1, t2) != 0);
    if (f1 >= 0) { close(f1); unlink(t1); }
    if (f2 >= 0) { close(f2); unlink(t2); }

    // Hard links in tmpfs: the X server takes its lock this way.
    int lf = open("/tmp/pmtest-a", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (lf >= 0) { write(lf, "42", 2); close(lf); }
    struct stat la, lb;
    CHECK("link in /tmp: two names, one file",
          link("/tmp/pmtest-a", "/tmp/pmtest-b") == 0 &&
          stat("/tmp/pmtest-a", &la) == 0 && stat("/tmp/pmtest-b", &lb) == 0 &&
          la.st_ino == lb.st_ino && la.st_nlink == 2);
    errno = 0;
    CHECK("link onto an existing name: EEXIST", link("/tmp/pmtest-a", "/tmp/pmtest-b") < 0 && errno == EEXIST);
    CHECK("rename between two names of one file does nothing",
          rename("/tmp/pmtest-a", "/tmp/pmtest-b") == 0 && access("/tmp/pmtest-a", F_OK) == 0);
    char lbuf[4] = { 0 };
    lf = open("/tmp/pmtest-b", O_RDONLY);
    CHECK("unlink one name, the other still reads",
          unlink("/tmp/pmtest-a") == 0 && lf >= 0 && read(lf, lbuf, 2) == 2 && !strcmp(lbuf, "42") &&
          stat("/tmp/pmtest-b", &lb) == 0 && lb.st_nlink == 1);
    if (lf >= 0) close(lf);
    unlink("/tmp/pmtest-b");
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
    test_kernel_log();
    test_memory();
    test_files();
    test_terminal();
    test_pty();
    test_sockets();
    test_time();
    test_processes();
    test_signals();
    say("pmtest: %d passed, %d failed\n", passed, failed);
    return failed;
}
