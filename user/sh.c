// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// ksh -- the Kunix ring 3 shell. The kernel starts /SH.ELF as its first
// process; when it exits, the kernel falls back to its built-in shell.
//
// Anything that is not a builtin is run as a program: "foo a b" spawns
// FOO.ELF (looked up in the working directory, then in /) with argv
// {"foo", "a", "b"} and waits for it. A trailing "&" runs it in the
// background instead.

#include "kunix.h"

#define LINE_MAX  256
#define MAX_ARGS  16
#define MAX_JOBS  16

static char file_buf[64 * 1024];
static long jobs[MAX_JOBS];          // pids of background processes, 0 = free

static int split(char* line, char** argv) {
    int argc = 0;
    char* p = line;
    while (*p) {
        while (*p == ' ') *p++ = '\0';
        if (!*p) break;
        if (argc == MAX_ARGS) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
    }
    argv[argc] = 0;
    return argc;
}

static void report_status(long pid, long status) {
    if (status == 0) return;
    kputs("[");
    kputi(pid);
    kputs("] exited with status ");
    kputi(status);
    if (status >= 128) kputs(" (killed by a fault)");
    kputs("\n");
}

// Collect background jobs that have finished, so they do not linger as
// zombies. wait() on a pid that is already a zombie returns at once.
static void reap_jobs(void) {
    kunix_procinfo_t info;
    for (u64 i = 0; sys_procinfo(i, &info) == 1; i++) {
        if (info.state != KUNIX_PROC_ZOMBIE) continue;
        for (int j = 0; j < MAX_JOBS; j++) {
            if (jobs[j] != info.pid) continue;
            long status;
            sys_wait(info.pid, &status);
            kputs("[");
            kputi(info.pid);
            kputs("] done, status ");
            kputi(status);
            kputs("\n");
            jobs[j] = 0;
            i = (u64)-1;             // the table shifted; start over
            break;
        }
    }
}

// "foo" -> "FOO.ELF"-ish: append .elf unless the name already has a dot.
static void program_path(const char* name, const char* dir, char* out) {
    int o = 0;
    for (int i = 0; dir[i]; i++) out[o++] = dir[i];
    int has_dot = 0;
    for (int i = 0; name[i] && o < 120; i++) {
        if (name[i] == '.') has_dot = 1;
        out[o++] = name[i];
    }
    if (!has_dot) {
        const char* ext = ".elf";
        for (int i = 0; ext[i]; i++) out[o++] = ext[i];
    }
    out[o] = '\0';
}

static void run_program(int argc, char** argv) {
    int background = 0;
    if (argc > 1 && kstrcmp(argv[argc - 1], "&") == 0) {
        background = 1;
        argv[--argc] = 0;
    }

    char path[128];
    program_path(argv[0], "", path);
    long pid = sys_spawn(path, argv);
    if (pid < 0) {
        program_path(argv[0], "/", path);
        pid = sys_spawn(path, argv);
    }
    if (pid < 0) {
        kputs(argv[0]);
        kputs(": command not found\n");
        return;
    }

    if (background) {
        for (int j = 0; j < MAX_JOBS; j++) {
            if (!jobs[j]) { jobs[j] = pid; break; }
        }
        kputs("[");
        kputi(pid);
        kputs("] started in the background\n");
        return;
    }

    long status;
    sys_wait(pid, &status);
    report_status(pid, status);
}

static void cmd_ls(const char* path) {
    kunix_dirent_t ent;
    long r;
    for (u64 i = 0; (r = sys_readdir(path, i, &ent)) == 1; i++) {
        kputs(ent.name);
        if (ent.is_dir) {
            kputs("/");
        } else {
            u64 pad = kstrlen(ent.name);
            do { kputs(" "); } while (++pad < 14);
            kputu(ent.size);
            kputs(" bytes");
        }
        kputs("\n");
    }
    if (r < 0) {
        kputs("ls: ");
        kputs(path ? path : ".");
        kputs(": no such directory\n");
    }
}

static void cmd_cat(const char* path) {
    long n = sys_readfile(path, file_buf, sizeof(file_buf));
    if (n < 0) {
        kputs("cat: ");
        kputs(path);
        kputs(": no such file\n");
        return;
    }
    sys_write(KUNIX_STDOUT, file_buf, (u64)n);
    if (n > 0 && file_buf[n - 1] != '\n') kputs("\n");
}

static void cmd_ps(void) {
    static const char* states[] = { "unused", "embryo", "ready", "running", "blocked", "zombie" };
    kunix_procinfo_t info;
    kputs("  PID  PPID  STATE    NAME\n");
    for (u64 i = 0; sys_procinfo(i, &info) == 1; i++) {
        char num[8];
        for (int col = 0; col < 2; col++) {
            long v = col ? info.ppid : info.pid;
            int k = 0;
            do { num[k++] = (char)('0' + v % 10); v /= 10; } while (v);
            for (int pad = k; pad < 5; pad++) kputs(" ");
            while (k) sys_write(KUNIX_STDOUT, &num[--k], 1);
            kputs(" ");
        }
        const char* st = (info.state >= 0 && info.state <= 5) ? states[info.state] : "?";
        kputs(" ");
        kputs(st);
        for (u64 pad = kstrlen(st); pad < 9; pad++) kputs(" ");
        kputs(info.name);
        kputs("\n");
    }
}

static void cmd_help(void) {
    kputs("ksh builtins:\n");
    kputs("  help          this list\n");
    kputs("  echo ARGS     print ARGS\n");
    kputs("  ls [DIR]      list a directory\n");
    kputs("  cat FILE      print a file\n");
    kputs("  cd DIR / pwd  change / show the working directory\n");
    kputs("  ps            list processes\n");
    kputs("  exit [N]      leave ksh (the kernel shell takes over)\n");
    kputs("Anything else runs NAME.ELF; end the line with & to background it.\n");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    kputs("\nksh: Kunix ring 3 shell, pid ");
    kputi(sys_getpid());
    kputs(". Type 'help' for builtins.\n\n");

    char line[LINE_MAX];
    char* args[MAX_ARGS + 1];
    char cwd[256];

    for (;;) {
        reap_jobs();

        if (sys_getcwd(cwd, sizeof(cwd)) < 0) { cwd[0] = '?'; cwd[1] = '\0'; }
        kputs("ksh:");
        kputs(cwd);
        kputs("$ ");

        long n = sys_read(KUNIX_STDIN, line, sizeof(line) - 1);
        if (n < 0) continue;
        line[n] = '\0';

        int nargs = split(line, args);
        if (nargs == 0) continue;
        const char* cmd = args[0];

        if (kstrcmp(cmd, "exit") == 0) {
            long status = 0;
            if (nargs > 1) {
                for (const char* p = args[1]; *p >= '0' && *p <= '9'; p++) {
                    status = status * 10 + (*p - '0');
                }
            }
            return (int)status;
        } else if (kstrcmp(cmd, "help") == 0) {
            cmd_help();
        } else if (kstrcmp(cmd, "echo") == 0) {
            for (int i = 1; i < nargs; i++) {
                kputs(args[i]);
                kputs(i + 1 < nargs ? " " : "");
            }
            kputs("\n");
        } else if (kstrcmp(cmd, "ls") == 0) {
            cmd_ls(nargs > 1 ? args[1] : 0);
        } else if (kstrcmp(cmd, "cat") == 0) {
            if (nargs < 2) kputs("usage: cat FILE\n");
            else cmd_cat(args[1]);
        } else if (kstrcmp(cmd, "cd") == 0) {
            if (sys_chdir(nargs > 1 ? args[1] : "/") < 0) {
                kputs("cd: no such directory\n");
            }
        } else if (kstrcmp(cmd, "pwd") == 0) {
            kputs(cwd);
            kputs("\n");
        } else if (kstrcmp(cmd, "ps") == 0) {
            cmd_ps();
        } else {
            run_program(nargs, args);
        }
    }
}
