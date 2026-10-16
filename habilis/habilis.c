// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// Habilis -- a small init system.
//
// It is an ordinary POSIX program (plus Linux's reboot(2) to end the
// system), so it does not care which kernel it runs on: built static
// against musl it boots Kunix through Prime Meridian, and would boot Linux
// the same way.
//
// The screen stays clear but for one line,
//
//     Starting <service>... /
//
// its spinner turning while the service runs. A service that fails leaves
// "[FAILED] <name>" on a line below, and failures stack downwards. Every
// service's own output goes to the log instead of the screen.
//
// Services are files in /etc/habilis, one each, named <name>.service:
//
//     description=hostname          what "Starting ..." says
//     exec=hostname -F /etc/hostname  run with /bin/sh -c
//     type=oneshot                  oneshot: must finish, and succeed
//                                   daemon:  started, restarted if it dies
//                                   console: the login shell, on the
//                                            terminal, once the rest is up;
//                                            started again when it exits
//     after=var home                these first
//     timeout=30                    oneshot: seconds before it has failed
//
// /etc/habilis/habilis.conf may name where the log goes (log=path, as many
// as wanted; default /var/log/habilis.log).
//
// As process 1 it also collects orphaned children, and answers the
// signals halt, poweroff and reboot send it (SIGUSR1, SIGUSR2, SIGTERM):
// stop everything, then reboot(2).

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CONF_DIR    "/etc/habilis"
#define MAX_SERVICES 64
#define MAX_LOGS     4
#define LINE_MAX_   512

enum { ONESHOT, DAEMON, CONSOLE };
enum { PENDING, VISITING, DONE, FAILED };

typedef struct {
    char name[64];
    char description[128];
    char exec[LINE_MAX_];
    char after[LINE_MAX_];
    int type;
    int timeout;
    int state;
    pid_t pid;                   // daemons and the console, while running
    time_t started;
    int quick_deaths;            // respawns that died at once, in a row
} service_t;

static service_t services[MAX_SERVICES];
static int nservices;
static char logs[MAX_LOGS][256];
static int nlogs;
static int failures;             // [FAILED] lines on the screen so far
static struct termios sane_tty;
static int have_tty;

static volatile sig_atomic_t shutdown_how = -1;     // RB_* once asked to stop

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

static void out(const char* s) {
    write(STDOUT_FILENO, s, strlen(s));
}

static void logf_(const char* fmt, ...) {
    char line[LINE_MAX_];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(line) - 2) n = sizeof(line) - 2;
    line[n++] = '\n';
    for (int i = 0; i < nlogs; i++) {
        int fd = open(logs[i], O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
        if (fd >= 0) {
            write(fd, "habilis: ", 9);
            write(fd, line, (size_t)n);
            close(fd);
        }
    }
}

// The status line: row 1, rewritten in place.
static void status(const char* what, char spin) {
    char buf[256];
    snprintf(buf, sizeof(buf), "\033[1;1H\033[2KStarting %s... %c", what, spin);
    out(buf);
}

static void failed(const char* name) {
    char buf[256];
    snprintf(buf, sizeof(buf), "\033[%d;1H\033[2K\033[31m[FAILED]\033[0m %s", 2 + failures, name);
    out(buf);
    failures++;
}

static void clear_screen(void) {
    out("\033[H\033[2J");
}

// One spinner for the whole boot (and shutdown): it keeps turning from one
// service to the next rather than starting over at '/' for each.
static unsigned spin_frame;
static char spin(void) {
    return "/|-\\"[spin_frame++ % 4];
}

// The terminal's cursor would sit blinking after the spinner.
static void cursor(int visible) {
    out(visible ? "\033[?25h" : "\033[?25l");
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

static char* trim(char* s) {
    while (*s == ' ' || *s == '\t') s++;
    char* e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = '\0';
    return s;
}

// Call each(key, value) for every "key=value" line of a file.
static int read_kv(const char* path, void (*each)(const char*, const char*, void*), void* ctx) {
    FILE* f = fopen(path, "r");
    if (!f) return -1;
    char line[LINE_MAX_];
    while (fgets(line, sizeof(line), f)) {
        char* l = trim(line);
        if (!*l || *l == '#') continue;
        char* eq = strchr(l, '=');
        if (!eq) continue;
        *eq = '\0';
        each(trim(l), trim(eq + 1), ctx);
    }
    fclose(f);
    return 0;
}

static void conf_kv(const char* k, const char* v, void* ctx) {
    (void)ctx;
    if (!strcmp(k, "log") && nlogs < MAX_LOGS) snprintf(logs[nlogs++], sizeof(logs[0]), "%s", v);
}

static void service_kv(const char* k, const char* v, void* ctx) {
    service_t* s = ctx;
    if (!strcmp(k, "description")) snprintf(s->description, sizeof(s->description), "%s", v);
    else if (!strcmp(k, "exec")) snprintf(s->exec, sizeof(s->exec), "%s", v);
    else if (!strcmp(k, "after")) snprintf(s->after, sizeof(s->after), "%s", v);
    else if (!strcmp(k, "timeout")) s->timeout = atoi(v);
    else if (!strcmp(k, "type")) s->type = !strcmp(v, "daemon") ? DAEMON : !strcmp(v, "console") ? CONSOLE : ONESHOT;
}

static int by_name(const void* a, const void* b) {
    return strcmp(((const service_t*)a)->name, ((const service_t*)b)->name);
}

static void load_services(void) {
    DIR* d = opendir(CONF_DIR);
    struct dirent* e;
    while (d && (e = readdir(d)) && nservices < MAX_SERVICES) {
        size_t n = strlen(e->d_name);
        if (n <= 8 || strcmp(e->d_name + n - 8, ".service")) continue;
        service_t* s = &services[nservices];
        memset(s, 0, sizeof(*s));
        snprintf(s->name, sizeof(s->name), "%.*s", (int)(n - 8), e->d_name);
        s->timeout = 30;
        char path[512];
        snprintf(path, sizeof(path), CONF_DIR "/%s", e->d_name);
        if (read_kv(path, service_kv, s) < 0 || !s->exec[0]) {
            logf_("%s: unreadable, or no exec=", path);
            continue;
        }
        if (!s->description[0]) strcpy(s->description, s->name);   // (name is the shorter)
        nservices++;
    }
    if (d) closedir(d);
    qsort(services, (size_t)nservices, sizeof(service_t), by_name);   // a stable order
}

static service_t* find(const char* name) {
    for (int i = 0; i < nservices; i++) if (!strcmp(services[i].name, name)) return &services[i];
    return NULL;
}

// ---------------------------------------------------------------------------
// Running things
// ---------------------------------------------------------------------------

// In a child: a clean slate of signals, and output to the log.
static void child_setup(int on_tty) {
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, NULL);
    for (int sig = 1; sig < NSIG; sig++) signal(sig, SIG_DFL);
    setsid();                    // its own session: ^C at boot is not its business
    if (on_tty) {
        ioctl(STDIN_FILENO, TIOCSCTTY, 0);
        tcsetpgrp(STDIN_FILENO, getpid());
        return;
    }
    int null = open("/dev/null", O_RDONLY);
    if (null >= 0) { dup2(null, STDIN_FILENO); close(null); }
    int log = -1;
    for (int i = 0; i < nlogs && log < 0; i++) {
        struct stat st;
        if (stat(logs[i], &st) == 0 && S_ISCHR(st.st_mode)) continue;   // a regular file, preferably
        log = open(logs[i], O_WRONLY | O_APPEND | O_CREAT, 0644);
    }
    if (log < 0) log = open("/dev/null", O_WRONLY);
    if (log >= 0) {
        dup2(log, STDOUT_FILENO);
        dup2(log, STDERR_FILENO);
        if (log > 2) close(log);
    }
}

static pid_t spawn(service_t* s, int on_tty) {
    pid_t pid = fork();
    if (pid == 0) {
        child_setup(on_tty);
        execl("/bin/sh", "sh", "-c", s->exec, (char*)0);
        _exit(127);
    }
    if (pid > 0) {
        s->pid = pid;
        s->started = time(NULL);
    }
    return pid;
}

static void sleep_ms(long ms) {
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000 };
    nanosleep(&t, NULL);
}

static void reap(pid_t pid, int status);

// Run a oneshot to the end, spinner turning. True if it succeeded.
static int run_oneshot(service_t* s) {
    logf_("starting %s: %s", s->name, s->exec);
    pid_t pid = spawn(s, 0);
    if (pid < 0) return 0;
    long waited = 0;
    for (;;) {
        status(s->description, spin());
        int st;
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) {
            s->pid = 0;
            if (WIFEXITED(st) && WEXITSTATUS(st) == 0) return 1;
            logf_("%s %s %d", s->name, WIFEXITED(st) ? "exited with status" : "was killed by signal",
                  WIFEXITED(st) ? WEXITSTATUS(st) : WTERMSIG(st));
            return 0;
        }
        if (r < 0 && errno != EINTR) return 0;
        if (waited >= (long)s->timeout * 1000) {
            logf_("%s timed out after %d s", s->name, s->timeout);
            kill(-pid, SIGKILL);
            waitpid(pid, NULL, 0);
            s->pid = 0;
            return 0;
        }
        sleep_ms(100);
        waited += 100;
    }
}

// Start s, after whatever it must come after. Console services wait for
// the end of boot.
static void start(service_t* s) {
    if (s->state == DONE || s->state == FAILED) return;
    if (s->state == VISITING) {                       // a cycle in after=
        logf_("%s: its after= goes round in a circle", s->name);
        s->state = FAILED;
        failed(s->name);
        return;
    }
    s->state = VISITING;
    char deps[LINE_MAX_];
    snprintf(deps, sizeof(deps), "%s", s->after);
    for (char* tok = strtok(deps, " \t,"); tok; tok = strtok(NULL, " \t,")) {
        service_t* d = find(tok);
        if (d) start(d);
        else logf_("%s: after=%s, which does not exist", s->name, tok);
    }
    if (s->type == CONSOLE) {
        s->state = PENDING;                           // later
        return;
    }
    int ok;
    if (s->type == DAEMON) {
        status(s->description, spin());
        logf_("starting %s: %s", s->name, s->exec);
        ok = spawn(s, 0) > 0;
    } else {
        ok = run_oneshot(s);
    }
    s->state = ok ? DONE : FAILED;
    if (!ok) failed(s->name);
}

static void start_console(service_t* s) {
    if (have_tty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &sane_tty);  // whatever the last one left
        tcsetpgrp(STDIN_FILENO, getpgrp());
    }
    pid_t pid = spawn(s, 1);
    logf_("started %s (pid %d)", s->name, (int)pid);
    if (pid > 0) s->state = DONE;
}

// A child is gone: if it was a daemon or the console, bring it back --
// unless it keeps dying straight away, in which case pause first.
static void reap(pid_t pid, int st) {
    for (int i = 0; i < nservices; i++) {
        service_t* s = &services[i];
        if (s->pid != pid) continue;
        s->pid = 0;
        logf_("%s (pid %d) %s %d", s->name, (int)pid, WIFEXITED(st) ? "exited with status" : "was killed by signal",
              WIFEXITED(st) ? WEXITSTATUS(st) : WTERMSIG(st));
        if (s->type == ONESHOT || shutdown_how >= 0) return;
        s->quick_deaths = time(NULL) - s->started < 2 ? s->quick_deaths + 1 : 0;
        if (s->quick_deaths >= 5) {
            logf_("%s keeps dying; waiting 10 s before the next try", s->name);
            sleep(10);
            s->quick_deaths = 0;
        }
        if (s->type == CONSOLE) {
            clear_screen();
            start_console(s);
        } else {
            spawn(s, 0);
        }
        return;
    }
}

// ---------------------------------------------------------------------------
// Going down
// ---------------------------------------------------------------------------

static void on_child(int sig) {
    (void)sig;                   // only to wake sigsuspend
}

static void on_signal(int sig) {
    if (sig == SIGUSR1) shutdown_how = RB_HALT_SYSTEM;
    else if (sig == SIGUSR2) shutdown_how = RB_POWER_OFF;
    else if (sig == SIGTERM) shutdown_how = RB_AUTOBOOT;
}

static void others_wait(long ms) {
    for (long t = 0; t < ms; t += 100) {
        while (waitpid(-1, NULL, WNOHANG) > 0) { }
        if (kill(-1, 0) < 0 && errno == ESRCH) return; // nobody left
        char buf[64];
        snprintf(buf, sizeof(buf), "\033[1;1H\033[2KStopping... %c", spin());
        out(buf);
        sleep_ms(100);
    }
}

static void go_down(void) {
    const char* what = shutdown_how == RB_AUTOBOOT ? "reboot" : shutdown_how == RB_POWER_OFF ? "power off" : "halt";
    logf_("asked to %s: stopping everything", what);
    clear_screen();
    cursor(0);
    // SIGTERM for programs, SIGHUP for interactive shells (which ignore
    // SIGTERM, and save their history on a hangup), SIGCONT so stopped
    // jobs hear either.
    kill(-1, SIGTERM);
    kill(-1, SIGHUP);
    kill(-1, SIGCONT);
    others_wait(5000);
    kill(-1, SIGKILL);
    others_wait(2000);
    sync();
    logf_("%s", what);
    out("\033[1;1H\033[2K");
    reboot(shutdown_how);
    logf_("reboot(2) failed: %s", strerror(errno));
    for (;;) pause();
}

// ---------------------------------------------------------------------------

int main(void) {
    if (getpid() != 1) {
        fprintf(stderr, "habilis: I am an init system: run me as process 1\n");
        return 1;
    }
    // Ignore what a terminal can send us; take the shutdown signals.
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    struct sigaction sa = { .sa_handler = on_signal };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    have_tty = tcgetattr(STDIN_FILENO, &sane_tty) == 0;
    if (!getenv("PATH")) setenv("PATH", "/sbin:/bin", 1);

    if (read_kv(CONF_DIR "/habilis.conf", conf_kv, NULL) < 0 || nlogs == 0) {
        snprintf(logs[0], sizeof(logs[0]), "/var/log/habilis.log");
        nlogs = 1;
    }
    mkdir("/var", 0755);
    mkdir("/var/log", 0755);

    clear_screen();
    cursor(0);
    logf_("starting");
    load_services();
    for (int i = 0; i < nservices && shutdown_how < 0; i++) start(&services[i]);

    // Boot is over. A clean boot leaves a clean screen; failures stay up
    // for the console to start beneath.
    cursor(1);
    if (failures == 0) clear_screen();
    else {
        char buf[64];
        snprintf(buf, sizeof(buf), "\033[1;1H\033[2K\033[%d;1H\n", 2 + failures);
        out(buf);
    }
    for (int i = 0; i < nservices && shutdown_how < 0; i++) {
        if (services[i].type == CONSOLE && services[i].state == PENDING) start_console(&services[i]);
    }
    logf_("up: %d service(s), %d failed", nservices, failures);

    // Process 1's job from here: wait, reap, respawn -- until told to stop.
    // The signals that matter stay blocked except inside sigsuspend, so
    // none can slip in between looking and going to sleep.
    struct sigaction chld = { .sa_handler = on_child };
    sigemptyset(&chld.sa_mask);
    sigaction(SIGCHLD, &chld, NULL);
    sigset_t block, waiting;
    sigemptyset(&block);
    sigaddset(&block, SIGCHLD);
    sigaddset(&block, SIGUSR1);
    sigaddset(&block, SIGUSR2);
    sigaddset(&block, SIGTERM);
    sigprocmask(SIG_BLOCK, &block, &waiting);
    for (;;) {
        int st;
        pid_t pid;
        while ((pid = waitpid(-1, &st, WNOHANG)) > 0) reap(pid, st);
        if (shutdown_how >= 0) go_down();
        sigsuspend(&waiting);
    }
}
