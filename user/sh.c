// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// ksh -- the Kunix shell. A stopgap until zsh can be ported (it needs a
// fuller libc and termios), so it borrows zsh's look: a `%` prompt and the
// same reactions to Ctrl+C and Ctrl+D.
//
// Grammar, roughly:
//   line     := list
//   list     := and_or ( (';' | '&') and_or )* [';' | '&']
//   and_or   := pipeline ( ('&&' | '||') pipeline )*
//   pipeline := command ( '|' command )*
//   command  := ( word | redirect )+
//   redirect := '<' word | '>' word | '>>' word | '2>' word | '2>&1'
// Words take '...' (literal), "..." (with $ expansion) and \x escapes.
// $NAME, ${NAME}, $? and $$ expand outside single quotes.
//
// Each pipeline runs as its own process group; a foreground one is handed
// the terminal (tcsetpgrp) so Ctrl+C reaches it rather than the shell.

#include "kunix.h"

#define MAX_TOKENS 128
#define MAX_ARGS   32
#define MAX_CMDS   8
#define MAX_JOBS   16

typedef enum { T_WORD, T_PIPE, T_AND, T_OR, T_SEMI, T_AMP, T_LT, T_GT, T_GTGT,
               T_ERR_GT, T_ERR_TO_OUT, T_END } tok_type_t;

typedef struct {
    tok_type_t type;
    char* text;                       // T_WORD only (malloc'd)
} token_t;

typedef struct {
    char* argv[MAX_ARGS + 1];
    int argc;
    char* in;                         // < file
    char* out;                        // > or >> file
    int append;
    char* err;                        // 2> file
    int err_to_out;                   // 2>&1
} command_t;

typedef struct {
    pid_t pgid;
    pid_t pids[MAX_CMDS];            // members not yet reaped
    int npids;                       // 0 = free slot
    pid_t last_pid;                  // the pipeline's last command: its status counts
    int last_status;
    int stopped;
    char text[64];
} job_t;

static int last_status = 0;
static pid_t shell_pgid;
static job_t jobs[MAX_JOBS];
static volatile int got_sigint = 0;

static void on_sigint(int sig) {
    (void)sig;
    got_sigint = 1;
}

// ---------------------------------------------------------------------------
// Lexing and expansion
// ---------------------------------------------------------------------------

typedef struct {
    char* buf;
    size_t len, cap;
} str_t;

static void str_push(str_t* s, char c) {
    if (s->len + 1 >= s->cap) {
        s->cap = s->cap ? s->cap * 2 : 32;
        s->buf = realloc(s->buf, s->cap);
    }
    s->buf[s->len++] = c;
    s->buf[s->len] = '\0';
}

static void str_append(str_t* s, const char* t) {
    while (*t) str_push(s, *t++);
}

// Expand a $ reference starting at *pp (just past the '$').
static void expand_var(const char** pp, str_t* out) {
    const char* p = *pp;
    char name[64];
    int n = 0;
    if (*p == '?') {
        char num[16];
        snprintf(num, sizeof(num), "%d", last_status);
        str_append(out, num);
        *pp = p + 1;
        return;
    }
    if (*p == '$') {
        char num[16];
        snprintf(num, sizeof(num), "%d", getpid());
        str_append(out, num);
        *pp = p + 1;
        return;
    }
    if (*p == '{') {
        p++;
        while (*p && *p != '}' && n < 63) name[n++] = *p++;
        if (*p == '}') p++;
    } else {
        while ((isalnum(*p) || *p == '_') && n < 63) name[n++] = *p++;
    }
    name[n] = '\0';
    if (n == 0) {
        str_push(out, '$');          // a lone '$' is just a dollar sign
    } else {
        const char* v = getenv(name);
        if (v) str_append(out, v);
    }
    *pp = p;
}

static int is_op_char(char c) {
    return c == '|' || c == '&' || c == ';' || c == '<' || c == '>';
}

// Returns the number of tokens, or -1 on a syntax error (unclosed quote).
static int lex(const char* line, token_t* toks) {
    int n = 0;
    const char* p = line;
    while (n < MAX_TOKENS - 1) {
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (!*p || *p == '#') break;

        token_t* t = &toks[n++];
        t->text = NULL;
        if (p[0] == '2' && p[1] == '>' && p[2] == '&' && p[3] == '1') { t->type = T_ERR_TO_OUT; p += 4; continue; }
        if (p[0] == '2' && p[1] == '>') { t->type = T_ERR_GT; p += 2; continue; }
        if (p[0] == '>' && p[1] == '>') { t->type = T_GTGT; p += 2; continue; }
        if (p[0] == '&' && p[1] == '&') { t->type = T_AND; p += 2; continue; }
        if (p[0] == '|' && p[1] == '|') { t->type = T_OR; p += 2; continue; }
        if (*p == '|') { t->type = T_PIPE; p++; continue; }
        if (*p == '&') { t->type = T_AMP; p++; continue; }
        if (*p == ';') { t->type = T_SEMI; p++; continue; }
        if (*p == '<') { t->type = T_LT; p++; continue; }
        if (*p == '>') { t->type = T_GT; p++; continue; }

        str_t w = { 0, 0, 0 };
        str_push(&w, '\0');
        w.len = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && !is_op_char(*p)) {
            if (*p == '\'') {
                p++;
                while (*p && *p != '\'') str_push(&w, *p++);
                if (*p != '\'') { free(w.buf); return -1; }
                p++;
            } else if (*p == '"') {
                p++;
                while (*p && *p != '"') {
                    if (*p == '\\' && (p[1] == '"' || p[1] == '\\' || p[1] == '$')) {
                        str_push(&w, p[1]);
                        p += 2;
                    } else if (*p == '$') {
                        p++;
                        expand_var(&p, &w);
                    } else {
                        str_push(&w, *p++);
                    }
                }
                if (*p != '"') { free(w.buf); return -1; }
                p++;
            } else if (*p == '\\' && p[1]) {
                str_push(&w, p[1]);
                p += 2;
            } else if (*p == '$') {
                p++;
                expand_var(&p, &w);
            } else {
                str_push(&w, *p++);
            }
        }
        t->type = T_WORD;
        t->text = w.buf;
    }
    toks[n].type = T_END;
    toks[n].text = NULL;
    return n;
}

static void free_tokens(token_t* toks, int n) {
    for (int i = 0; i < n; i++) free(toks[i].text);
}

// ---------------------------------------------------------------------------
// Builtins
// ---------------------------------------------------------------------------

static void report(pid_t pid, int status, const char* prefix);

static int builtin_cd(command_t* c) {
    const char* dir = c->argc > 1 ? c->argv[1] : getenv("HOME");
    if (!dir) dir = "/";
    if (chdir(dir) < 0) {
        dprintf(STDERR_FILENO, "cd: %s: %s\n", dir, strerror(errno));
        return 1;
    }
    char cwd[256];
    if (getcwd(cwd, sizeof(cwd))) setenv("PWD", cwd, 1);
    return 0;
}

static int builtin_export(command_t* c) {
    for (int i = 1; i < c->argc; i++) {
        char* eq = strchr(c->argv[i], '=');
        if (!eq) continue;
        *eq = '\0';
        setenv(c->argv[i], eq + 1, 1);
        *eq = '=';
    }
    return 0;
}

static int builtin_jobs(void) {
    for (int j = 0; j < MAX_JOBS; j++) {
        if (jobs[j].npids) {
            printf("[%d]  %-10s %s\n", j + 1, jobs[j].stopped ? "suspended" : "running", jobs[j].text);
        }
    }
    return 0;
}

// Wait for a foreground job: until every member has exited, or one stops.
// Returns the status of the job's last command, or -1 if it was suspended.
static int wait_foreground(job_t* job) {
    tcsetpgrp(STDIN_FILENO, job->pgid);
    int stopped = 0;
    while (job->npids && !stopped) {
        int st;
        pid_t pid = waitpid(-job->pgid, &st, WUNTRACED);
        if (pid < 0) {
            if (errno == EINTR) continue;
            break;                   // ECHILD: nothing left to wait for
        }
        if (WIFSTOPPED(st)) {
            stopped = 1;
            break;
        }
        for (int i = 0; i < job->npids; i++) {
            if (job->pids[i] != pid) continue;
            job->pids[i] = job->pids[--job->npids];
            break;
        }
        if (pid == job->last_pid) job->last_status = st;
        else if (WIFSIGNALED(st) && WTERMSIG(st) != SIGPIPE) report(pid, st, "killed");
    }
    tcsetpgrp(STDIN_FILENO, shell_pgid);
    if (stopped) {
        job->stopped = 1;
        return -1;
    }
    report(job->last_pid, job->last_status, "killed");
    return job->last_status;
}

static int status_code(int st) {
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    return WEXITSTATUS(st);
}

// Park a suspended foreground job in the table so fg/bg can find it.
static int remember_job(job_t* job) {
    for (int j = 0; j < MAX_JOBS; j++) {
        if (jobs[j].npids) continue;
        jobs[j] = *job;
        return j;
    }
    return -1;
}

// "%2" or "2" -> job index; no argument -> the most recent job.
static int pick_job(command_t* c) {
    if (c->argc > 1) {
        const char* a = c->argv[1];
        if (*a == '%') a++;
        int j = atoi(a) - 1;
        return (j >= 0 && j < MAX_JOBS && jobs[j].npids) ? j : -1;
    }
    for (int j = MAX_JOBS - 1; j >= 0; j--) if (jobs[j].npids) return j;
    return -1;
}

static int builtin_fg(command_t* c) {
    int j = pick_job(c);
    if (j < 0) { dprintf(STDERR_FILENO, "fg: no such job\n"); return 1; }
    job_t job = jobs[j];
    jobs[j].npids = 0;               // it lives on the stack while in front
    printf("[%d]  - continued  %s\n", j + 1, job.text);
    job.stopped = 0;
    kill(-job.pgid, SIGCONT);
    int st = wait_foreground(&job);
    if (st < 0) {
        jobs[j] = job;               // suspended again: same slot
        printf("\n[%d]  + suspended  %s\n", j + 1, job.text);
        return 128 + SIGTSTP;
    }
    return status_code(st);
}

static int builtin_bg(command_t* c) {
    int j = pick_job(c);
    if (j < 0) { dprintf(STDERR_FILENO, "bg: no such job\n"); return 1; }
    jobs[j].stopped = 0;
    kill(-jobs[j].pgid, SIGCONT);
    printf("[%d]  - continued  %s\n", j + 1, jobs[j].text);
    return 0;
}

static int builtin_help(void) {
    puts("ksh builtins: cd [dir], pwd, echo, export NAME=VALUE, unset NAME,");
    puts("              jobs, fg [%n], bg [%n], wait, exec [cmd], exit [n], help");
    puts("Keys: Ctrl+C interrupts, Ctrl+Z suspends, Ctrl+D at the prompt exits.");
    puts("Syntax: a | b, a > f, a >> f, a < f, a 2> f, a 2>&1, a; b, a && b,");
    puts("        a || b, a &, 'quotes', \"$VARS\", $? and $$.");
    puts("Programs are found in $PATH (currently: see `echo $PATH`).");
    return 0;
}

// Builtins that must run in the shell itself (they change its state).
// Returns -1 if `c` is not one of them.
static int run_special_builtin(command_t* c) {
    const char* n = c->argv[0];
    if (strcmp(n, "cd") == 0) return builtin_cd(c);
    if (strcmp(n, "export") == 0) return builtin_export(c);
    if (strcmp(n, "unset") == 0) {
        for (int i = 1; i < c->argc; i++) unsetenv(c->argv[i]);
        return 0;
    }
    if (strcmp(n, "exit") == 0) exit(c->argc > 1 ? atoi(c->argv[1]) : last_status);
    if (strcmp(n, "jobs") == 0) return builtin_jobs();
    if (strcmp(n, "fg") == 0) return builtin_fg(c);
    if (strcmp(n, "bg") == 0) return builtin_bg(c);
    if (strcmp(n, "wait") == 0) {
        int status;
        pid_t pid;
        while ((pid = waitpid(-1, &status, 0)) > 0) report(pid, status, "done");
        for (int j = 0; j < MAX_JOBS; j++) jobs[j].npids = 0;
        return 0;
    }
    // `exec` replaces the shell itself, which is how Habilis starts every
    // service (`exec /bin/netconfig ...` through `sh -c`). It must run
    // without forking, so it lives here and not with the plain builtins.
    if (strcmp(n, "exec") == 0) {
        if (c->argc < 2) return 0;      // redirections (if any) are already applied
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        execvp(c->argv[1], &c->argv[1]);
        int error = errno;      // diagnostics can fail too (for example fd 2 closed)
        dprintf(STDERR_FILENO, "ksh: %s: %s\n", c->argv[1],
                error == ENOENT ? "command not found" : strerror(error));
        exit(error == ENOENT ? 127 : 126);
    }
    return -1;
}

// Builtins that behave like ordinary programs (can sit in a pipeline).
static int run_plain_builtin(command_t* c) {
    const char* n = c->argv[0];
    if (strcmp(n, "echo") == 0) {
        int newline = 1, i = 1;
        if (c->argc > 1 && strcmp(c->argv[1], "-n") == 0) { newline = 0; i = 2; }
        for (; i < c->argc; i++) {
            fputs_fd(c->argv[i], STDOUT_FILENO);
            if (i + 1 < c->argc) write(STDOUT_FILENO, " ", 1);
        }
        if (newline) write(STDOUT_FILENO, "\n", 1);
        return 0;
    }
    if (strcmp(n, "pwd") == 0) {
        char cwd[256];
        if (!getcwd(cwd, sizeof(cwd))) { perror("pwd"); return 1; }
        puts(cwd);
        return 0;
    }
    if (strcmp(n, "help") == 0) return builtin_help();
    if (strcmp(n, "true") == 0) return 0;
    if (strcmp(n, "false") == 0) return 1;
    // In a pipeline the shell already forked, so replacing this child is
    // still the right semantics; the parent-side case lives with the
    // special builtins above.
    if (strcmp(n, "exec") == 0) {
        if (c->argc < 2) return 0;
        execvp(c->argv[1], &c->argv[1]);
        int error = errno;
        dprintf(STDERR_FILENO, "ksh: %s: %s\n", c->argv[1],
                error == ENOENT ? "command not found" : strerror(error));
        return error == ENOENT ? 127 : 126;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Running commands
// ---------------------------------------------------------------------------

static void report(pid_t pid, int status, const char* prefix) {
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        if (sig == SIGINT) return;    // like zsh: ^C already said it all
        dprintf(STDERR_FILENO, "ksh: %s (pid %d): %s\n", prefix, pid, signame(sig));
    }
}

static int redirect(int fd, const char* path, int flags) {
    int f = open(path, flags, 0644);
    if (f < 0) {
        dprintf(STDERR_FILENO, "ksh: %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (f != fd) {
        dup2(f, fd);
        close(f);
    }
    return 0;
}

static int apply_redirects(command_t* c) {
    if (c->in && redirect(STDIN_FILENO, c->in, O_RDONLY) < 0) return -1;
    if (c->out && redirect(STDOUT_FILENO, c->out,
                           O_WRONLY | O_CREAT | (c->append ? O_APPEND : O_TRUNC)) < 0) return -1;
    if (c->err && redirect(STDERR_FILENO, c->err, O_WRONLY | O_CREAT | O_TRUNC) < 0) return -1;
    if (c->err_to_out) dup2(STDOUT_FILENO, STDERR_FILENO);
    return 0;
}

// The child side of fork: wire up fds, restore default signals, exec.
__attribute__((noreturn))
static void child_exec(command_t* c, pid_t pgid, int in_fd, int out_fd) {
    signal(SIGINT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);        // the shell ignores it; its jobs must not
    setpgid(0, pgid);
    if (in_fd != STDIN_FILENO) { dup2(in_fd, STDIN_FILENO); close(in_fd); }
    if (out_fd != STDOUT_FILENO) { dup2(out_fd, STDOUT_FILENO); close(out_fd); }
    if (apply_redirects(c) < 0) _exit(1);

    int r = run_plain_builtin(c);
    if (r >= 0) _exit(r);
    execvp(c->argv[0], c->argv);
    dprintf(STDERR_FILENO, "ksh: %s: %s\n", c->argv[0],
            errno == ENOENT ? "command not found" : strerror(errno));
    _exit(errno == ENOENT ? 127 : 126);
}

static int run_pipeline(command_t* cmds, int ncmds, int background, const char* text) {
    // A lone special builtin runs in the shell. exec keeps its redirections
    // on this process, including when there is no replacement command.
    if (ncmds == 1 && cmds[0].argc > 0) {
        command_t* c = &cmds[0];
        const char* n = c->argv[0];
        int special = strcmp(n, "cd") == 0 || strcmp(n, "export") == 0 ||
                      strcmp(n, "unset") == 0 || strcmp(n, "exit") == 0 ||
                      strcmp(n, "jobs") == 0 || strcmp(n, "wait") == 0 ||
                      strcmp(n, "fg") == 0 || strcmp(n, "bg") == 0 ||
                      strcmp(n, "exec") == 0;
        if (special && !background) {
            if (!strcmp(n, "exec") && apply_redirects(c) < 0) return 1;
            return run_special_builtin(c);
        }
    }

    pid_t pids[MAX_CMDS];
    pid_t pgid = 0;
    int in_fd = STDIN_FILENO;
    for (int i = 0; i < ncmds; i++) {
        int fds[2] = { -1, -1 };
        int out_fd = STDOUT_FILENO;
        if (i + 1 < ncmds) {
            if (pipe(fds) < 0) { perror("ksh: pipe"); return 1; }
            out_fd = fds[1];
        }

        pid_t pid = fork();
        if (pid < 0) { perror("ksh: fork"); return 1; }
        if (pid == 0) {
            if (fds[0] >= 0) close(fds[0]);
            child_exec(&cmds[i], pgid, in_fd, out_fd);
        }
        if (!pgid) pgid = pid;
        setpgid(pid, pgid);          // also done by the child; whoever is first wins
        pids[i] = pid;

        if (in_fd != STDIN_FILENO) close(in_fd);
        if (out_fd != STDOUT_FILENO) close(out_fd);
        in_fd = fds[0] >= 0 ? fds[0] : STDIN_FILENO;
    }

    job_t job;
    memset(&job, 0, sizeof(job));
    job.pgid = pgid;
    job.npids = ncmds;
    for (int i = 0; i < ncmds; i++) job.pids[i] = pids[i];
    job.last_pid = pids[ncmds - 1];
    strncpy(job.text, text, sizeof(job.text) - 1);

    if (background) {
        int j = remember_job(&job);
        printf("[%d] %d\n", j + 1, pgid);
        return 0;
    }

    int st = wait_foreground(&job);
    if (st < 0) {
        int j = remember_job(&job);
        printf("\n[%d]  + suspended  %s\n", j + 1, job.text);
        return 128 + SIGTSTP;
    }
    return status_code(st);
}

// Parse tokens[*pos..] into one pipeline; returns its exit status.
static int parse_and_run_pipeline(token_t* toks, int* pos, int background_hint, const char* text) {
    command_t cmds[MAX_CMDS];
    int ncmds = 0;
    memset(cmds, 0, sizeof(cmds));

    for (;;) {
        if (ncmds == MAX_CMDS) { dprintf(STDERR_FILENO, "ksh: pipeline too long\n"); return 1; }
        command_t* c = &cmds[ncmds++];
        while (toks[*pos].type != T_END && toks[*pos].type != T_PIPE &&
               toks[*pos].type != T_AND && toks[*pos].type != T_OR &&
               toks[*pos].type != T_SEMI && toks[*pos].type != T_AMP) {
            token_t* t = &toks[(*pos)++];
            if (t->type == T_WORD) {
                if (c->argc < MAX_ARGS) c->argv[c->argc++] = t->text;
                continue;
            }
            if (t->type == T_ERR_TO_OUT) { c->err_to_out = 1; continue; }
            token_t* target = &toks[*pos];
            if (target->type != T_WORD) { dprintf(STDERR_FILENO, "ksh: missing file after redirection\n"); return 2; }
            (*pos)++;
            if (t->type == T_LT) c->in = target->text;
            else if (t->type == T_GT) { c->out = target->text; c->append = 0; }
            else if (t->type == T_GTGT) { c->out = target->text; c->append = 1; }
            else if (t->type == T_ERR_GT) c->err = target->text;
        }
        c->argv[c->argc] = NULL;
        if (c->argc == 0) { dprintf(STDERR_FILENO, "ksh: syntax error: empty command\n"); return 2; }
        if (toks[*pos].type != T_PIPE) break;
        (*pos)++;
    }
    return run_pipeline(cmds, ncmds, background_hint, text);
}

// Is the and_or list starting at pos followed by '&'?
static int ends_in_amp(token_t* toks, int pos) {
    while (toks[pos].type != T_END && toks[pos].type != T_SEMI && toks[pos].type != T_AMP) pos++;
    return toks[pos].type == T_AMP;
}

static void run_line(const char* line) {
    token_t toks[MAX_TOKENS];
    int n = lex(line, toks);
    if (n < 0) { dprintf(STDERR_FILENO, "ksh: unmatched quote\n"); last_status = 2; return; }
    if (n == 0) return;

    char text[64];
    strncpy(text, line, sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
    char* nl = strchr(text, '\n');
    if (nl) *nl = '\0';

    int pos = 0;
    while (toks[pos].type != T_END) {
        int bg = ends_in_amp(toks, pos);
        int status = parse_and_run_pipeline(toks, &pos, bg, text);
        // && and || chain on the previous status.
        while (toks[pos].type == T_AND || toks[pos].type == T_OR) {
            int is_and = toks[pos].type == T_AND;
            pos++;
            if ((is_and && status == 0) || (!is_and && status != 0)) {
                status = parse_and_run_pipeline(toks, &pos, bg, text);
            } else {
                // Skip the pipeline without running it.
                while (toks[pos].type != T_END && toks[pos].type != T_AND &&
                       toks[pos].type != T_OR && toks[pos].type != T_SEMI &&
                       toks[pos].type != T_AMP) pos++;
            }
        }
        last_status = status;
        if (toks[pos].type == T_SEMI || toks[pos].type == T_AMP) pos++;
        else if (toks[pos].type != T_END) {
            dprintf(STDERR_FILENO, "ksh: syntax error\n");
            last_status = 2;
            break;
        }
    }
    free_tokens(toks, n);
}

// Collect finished background jobs before each prompt, like zsh does.
static void reap_jobs(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        for (int j = 0; j < MAX_JOBS; j++) {
            for (int i = 0; i < jobs[j].npids; i++) {
                if (jobs[j].pids[i] != pid) continue;
                if (WIFSTOPPED(status)) {
                    if (!jobs[j].stopped) printf("[%d]  + suspended  %s\n", j + 1, jobs[j].text);
                    jobs[j].stopped = 1;
                    break;
                }
                jobs[j].pids[i] = jobs[j].pids[--jobs[j].npids];
                if (jobs[j].npids == 0) {
                    printf("[%d]  + %s  %s\n", j + 1,
                           WIFSIGNALED(status) ? signame(WTERMSIG(status)) : "done",
                           jobs[j].text);
                }
                break;
            }
        }
    }
}

static void prompt(void) {
    char cwd[256];
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, "?");
    const char* home = getenv("HOME");
    const char* shown = cwd;
    char tilde[260];
    // zsh's %~: show $HOME as ~ (unless HOME is just "/").
    if (home && strcmp(home, "/") != 0) {
        size_t h = strlen(home);
        if (strncmp(cwd, home, h) == 0 && (cwd[h] == '\0' || cwd[h] == '/')) {
            snprintf(tilde, sizeof(tilde), "~%s", cwd + h);
            shown = tilde;
        }
    }
    printf("%s@kunix %s %% ", getenv("USER") ? getenv("USER") : "root", shown);
}

int main(int argc, char** argv, char** envp) {
    (void)envp;

    if (!getenv("PATH"))  setenv("PATH", "/bin:/fat/BIN", 1);
    if (!getenv("HOME"))  setenv("HOME", "/", 1);
    if (!getenv("SHELL")) setenv("SHELL", "/bin/sh", 1);
    if (!getenv("TERM"))  setenv("TERM", "linux", 1);   // what drivers/vt.c emulates
    if (!getenv("USER"))  setenv("USER", "root", 1);

    // `sh -c "command"` runs one line and exits.
    if (argc > 2 && strcmp(argv[1], "-c") == 0) {
        run_line(argv[2]);
        return last_status;
    }

    shell_pgid = getpid();
    setpgid(0, shell_pgid);
    tcsetpgrp(STDIN_FILENO, shell_pgid);
    // Ctrl+C at the prompt should just give a fresh prompt.
    signal(SIGINT, on_sigint);
    // Ctrl+Z at the prompt must not suspend the shell itself.
    signal(SIGTSTP, SIG_IGN);

    struct utsname u;
    if (uname(&u) == 0) printf("\n%s %s (%s) -- ksh, pid %d. Type 'help'.\n\n",
                               u.sysname, u.release, u.machine, getpid());

    char line[512];
    for (;;) {
        reap_jobs();
        prompt();
        got_sigint = 0;
        ssize_t n = read(STDIN_FILENO, line, sizeof(line) - 1);
        if (n < 0) {
            if (errno == EINTR) continue;           // ^C: the tty already echoed it
            perror("ksh: read");
            return 1;
        }
        if (n == 0) {                               // ^D on an empty line
            puts("");
            return last_status;
        }
        line[n] = '\0';
        run_line(line);
    }
}
