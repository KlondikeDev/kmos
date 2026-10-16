// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// permtest -- users and permissions. Run as root; each check forks a child
// that becomes an ordinary user (uid/gid 1000, no extra groups) and tries
// something it should or should not be allowed to do.

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define USER 1000

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

// Run fn as the ordinary user; its return value is the exit status.
static int as_user(int (*fn)(void)) {
    pid_t pid = fork();
    if (pid == 0) {
        if (setgroups(0, NULL) || setgid(USER) || setuid(USER)) _exit(99);
        _exit(fn());
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

// Each returns 0 for "as expected".
static int u_ids(void) {
    return getuid() == USER && geteuid() == USER && getgid() == USER ? 0 : 1;
}
static int u_no_setuid0(void) {
    return setuid(0) < 0 && errno == EPERM && geteuid() == USER ? 0 : 1;
}
static int u_read_shadow(void) {
    return open("/etc/shadow", O_RDONLY) < 0 && errno == EACCES ? 0 : 1;
}
static int u_write_etc(void) {
    return open("/etc/hostname", O_WRONLY) < 0 && errno == EACCES &&
           open("/etc/newfile", O_WRONLY | O_CREAT, 0644) < 0 && errno == EACCES ? 0 : 1;
}
static int u_private_dir(void) {
    return open("/tmp/permtest/private/f", O_RDONLY) < 0 && errno == EACCES &&
           access("/tmp/permtest/private", X_OK) < 0 ? 0 : 1;
}
static int u_tmp_sticky(void) {
    int fd = open("/tmp/permtest-user-file", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 1;
    close(fd);
    struct stat st;
    if (stat("/tmp/permtest-user-file", &st) || st.st_uid != USER || st.st_gid != USER) return 2;
    // root's file in sticky /tmp: not ours to remove or rename away
    if (!(unlink("/tmp/permtest-roots-file") < 0 && errno == EPERM)) return 3;
    if (!(rename("/tmp/permtest-roots-file", "/tmp/stolen") < 0 && errno == EPERM)) return 4;
    return unlink("/tmp/permtest-user-file") == 0 ? 0 : 5;
}
static int u_chmod_chown(void) {
    if (!(chmod("/tmp/permtest-roots-file", 0777) < 0 && errno == EPERM)) return 1;
    int fd = open("/tmp/permtest-mine", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return 2;
    close(fd);
    if (chmod("/tmp/permtest-mine", 0640) != 0) return 3;
    if (!(chown("/tmp/permtest-mine", 0, 0) < 0 && errno == EPERM)) return 4;
    return unlink("/tmp/permtest-mine") == 0 ? 0 : 5;
}
static int u_exec_noperm(void) {
    return execl("/tmp/permtest/rootonly", "rootonly", (char*)0) < 0 && errno == EACCES ? 0 : 1;
}
static int u_kill(void) {
    if (!(kill(1, SIGTERM) < 0 && errno == EPERM)) return 1;      // not init
    pid_t pid = fork();
    if (pid == 0) { for (;;) pause(); }
    if (kill(pid, SIGKILL) != 0) return 2;                         // our own: fine
    waitpid(pid, NULL, 0);
    return 0;
}
static int u_admin(void) {
    return reboot(RB_POWER_OFF) < 0 && errno == EPERM &&
           sethostname("x", 1) < 0 && errno == EPERM &&
           setgroups(0, NULL) < 0 && errno == EPERM ? 0 : 1;
}
static int u_access_real(void) {
    // access() judges by the real ids: here both are the user's.
    return access("/etc/shadow", R_OK) < 0 && access("/etc/passwd", R_OK) == 0 &&
           access("/tmp", W_OK) == 0 ? 0 : 1;
}
static int u_suid(void) {
    execl("/tmp/permtest/suid", "suid", "--report", (char*)0);
    return 7;
}
static int u_utimes(void) {
    // Only the owner (or root) may set explicit times; "now" needs write access.
    return utimensat(AT_FDCWD, "/tmp/permtest-roots-file", (struct timespec[2]){ { 1, 0 }, { 1, 0 } }, 0) < 0 &&
           errno == EPERM ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "--report")) {
        // Run as a set-user-ID copy by uid 1000: are we root in effect?
        return geteuid() == 0 && getuid() == USER && getauxval(AT_SECURE) ? 42 : 3;
    }
    serial = fopen("/dev/serial", "w");
    say("permtest: starting\n");
    if (getuid() != 0) { say("permtest: run me as root\n"); return 1; }

    mkdir("/tmp/permtest", 0755);
    mkdir("/tmp/permtest/private", 0700);
    int fd = open("/tmp/permtest/private/f", O_WRONLY | O_CREAT, 0644);
    if (fd >= 0) close(fd);
    fd = open("/tmp/permtest-roots-file", O_WRONLY | O_CREAT, 0644);
    if (fd >= 0) close(fd);
    fd = open("/tmp/permtest/rootonly", O_WRONLY | O_CREAT | O_TRUNC, 0700);
    if (fd >= 0) { write(fd, "#!/bin/sh\nexit 0\n", 17); close(fd); }
    // A set-user-ID root copy of this very program.
    int in = open("/proc/self/exe", O_RDONLY), out = open("/tmp/permtest/suid", O_WRONLY | O_CREAT | O_TRUNC, 0755);
    char buf[8192];
    ssize_t n;
    while (in >= 0 && out >= 0 && (n = read(in, buf, sizeof(buf))) > 0) write(out, buf, (size_t)n);
    if (in >= 0) close(in);
    if (out >= 0) close(out);
    chmod("/tmp/permtest/suid", 04755);

    CHECK("dropping to an ordinary user", as_user(u_ids) == 0);
    CHECK("which cannot become root again", as_user(u_no_setuid0) == 0);
    CHECK("cannot read /etc/shadow", as_user(u_read_shadow) == 0);
    CHECK("cannot write in /etc", as_user(u_write_etc) == 0);
    CHECK("cannot look inside another user's 0700 directory", as_user(u_private_dir) == 0);
    CHECK("owns what it creates; the sticky bit protects root's files in /tmp", as_user(u_tmp_sticky) == 0);
    CHECK("chmod only its own files, chown nothing", as_user(u_chmod_chown) == 0);
    CHECK("cannot run what it may not execute", as_user(u_exec_noperm) == 0);
    CHECK("signals its own processes, not others'", as_user(u_kill) == 0);
    CHECK("reboot, sethostname, setgroups are root's", as_user(u_admin) == 0);
    CHECK("access() checks", as_user(u_access_real) == 0);
    CHECK("explicit times only by the owner", as_user(u_utimes) == 0);
    CHECK("a set-user-ID program runs as its owner (and AT_SECURE)", as_user(u_suid) == 42);

    // Root: everything, except running what has no execute bit at all.
    fd = open("/tmp/permtest/noexec", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) { write(fd, "#!/bin/sh\n", 10); close(fd); }
    errno = 0;
    CHECK("even root needs an execute bit", execl("/tmp/permtest/noexec", "noexec", (char*)0) < 0 && errno == EACCES);
    CHECK("root reads a 0700 directory of anyone's", access("/tmp/permtest/private/f", R_OK) == 0);

    unlink("/tmp/permtest/private/f");
    rmdir("/tmp/permtest/private");
    unlink("/tmp/permtest/rootonly");
    unlink("/tmp/permtest/noexec");
    unlink("/tmp/permtest/suid");
    rmdir("/tmp/permtest");
    unlink("/tmp/permtest-roots-file");
    say("permtest: %d passed, %d failed\n", passed, failed);
    return failed;
}
