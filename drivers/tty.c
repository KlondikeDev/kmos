// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// The line discipline, driven by a termios, for any terminal.
//
// Input (tty_input: the keyboard IRQ for the console, writes to the master
// for a pty) is translated (ICRNL, ...), then checked for signal characters
// (ISIG), edited into a line (ICANON), or queued for readers byte by byte.
// Readers block in tty_read. Output -- program writes and echo alike --
// goes through OPOST processing to the terminal's output function: the VT
// emulator for the console, the master's buffer for a pty.
//
// Syscalls run with interrupts off on our single CPU, so the keyboard IRQ
// can never run in the middle of a read: no locking needed.

#include <types.h>
#include <tty.h>
#include <vt.h>
#include <console.h>
#include <proc.h>
#include <signal.h>
#include <errno.h>
#include <memcpy.h>
#include <pit.h>

tty_t tty_console;

static const tty_termios_t sane = {
    .c_iflag = ICRNL | IXON,
    .c_oflag = OPOST | ONLCR,
    .c_cflag = B38400 | CS8 | CREAD | HUPCL,
    .c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN,
    .c_cc = {
        [VINTR] = 0x03, [VQUIT] = 0x1C, [VERASE] = 0x7F, [VKILL] = 0x15,
        [VEOF] = 0x04, [VTIME] = 0, [VMIN] = 1, [VSTART] = 0x11, [VSTOP] = 0x13,
        [VSUSP] = 0x1A, [VREPRINT] = 0x12, [VWERASE] = 0x17, [VLNEXT] = 0x16,
    },
};

void tty_setup(tty_t* t, bool (*output)(tty_t*, char), void (*flush)(tty_t*), void* priv) {
    t->tio = sane;
    t->fg_pgid = 0;
    t->sid = 0;
    t->rows = 24;
    t->cols = 80;
    t->hung_up = false;
    t->output = output;
    t->flush = flush;
    t->priv = priv;
    t->line_len = t->q_head = t->q_count = 0;
    t->eof_pending = t->literal_next = false;
}

static bool console_output(tty_t* t, char c) {
    (void)t;
    vt_putc(c);
    return true;
}

static void console_flush(tty_t* t) {
    (void)t;
    vt_flush();
}

void tty_init(void) {
    i32 fg = tty_console.fg_pgid, sid = tty_console.sid;
    bool again = tty_console.output != NULL;
    tty_setup(&tty_console, console_output, console_flush, NULL);
    tty_console.rows = (u16)characters_tall;
    tty_console.cols = (u16)characters_wide;
    if (again) {                          // a reset, not first boot: keep its owners
        tty_console.fg_pgid = fg;
        tty_console.sid = sid;
    }
}

// ---- output ----

static bool out_raw(tty_t* t, char c) {
    return t->output(t, c);
}

static bool out_char(tty_t* t, char c) {
    if ((t->tio.c_oflag & OPOST) && c == '\n' && (t->tio.c_oflag & ONLCR)) {
        if (!out_raw(t, '\r')) return false;
    } else if ((t->tio.c_oflag & OPOST) && c == '\r' && (t->tio.c_oflag & OCRNL)) {
        c = '\n';
    }
    return out_raw(t, c);
}

i64 tty_write(tty_t* t, const void* buf, u64 len, bool nonblock) {
    const char* p = (const char*)buf;
    u64 done = 0;
    while (done < len) {
        if (t->hung_up) return done ? (i64)done : -EIO;
        if (out_char(t, p[done])) { done++; continue; }
        // The other end is full: let it drain.
        t->flush(t);
        if (nonblock) return done ? (i64)done : -EAGAIN;
        sched_block_on(t);
        if (signal_pending(proc_current())) return done ? (i64)done : -EINTR;
    }
    t->flush(t);
    return (i64)len;
}

// Echo one input byte the way a terminal shows it: control characters as
// ^X when ECHOCTL is set.
static void echo_char(tty_t* t, char c) {
    if (!(t->tio.c_lflag & ECHO)) {
        if (c == '\n' && (t->tio.c_lflag & ECHONL)) out_char(t, '\n');
        return;
    }
    u8 u = (u8)c;
    if ((t->tio.c_lflag & ECHOCTL) && (u < 0x20 || u == 0x7F) && c != '\n' && c != '\t') {
        out_char(t, '^');
        out_char(t, u == 0x7F ? '?' : (char)(u + '@'));
    } else {
        out_char(t, c);
    }
}

static void echo_str(tty_t* t, const char* s) {
    while (*s) echo_char(t, *s++);
}

// Visually remove the last character of the line being edited.
static void echo_erase(tty_t* t, char gone) {
    if (!(t->tio.c_lflag & ECHO)) return;
    u32 cols = ((t->tio.c_lflag & ECHOCTL) && ((u8)gone < 0x20 || (u8)gone == 0x7F) && gone != '\t') ? 2 : 1;
    for (u32 i = 0; i < cols; i++) {
        if (t->tio.c_lflag & ECHOE) { out_char(t, '\b'); out_char(t, ' '); out_char(t, '\b'); }
        else out_char(t, '\b');
    }
}

// ---- input ----

static void enqueue(tty_t* t, const char* s, u32 n) {
    for (u32 i = 0; i < n && t->q_count < TTY_QUEUE_MAX; i++) {
        t->queue[(t->q_head + t->q_count) % TTY_QUEUE_MAX] = s[i];
        t->q_count++;
    }
    sched_wake_obj(t->queue);
}

static void raise_signal(tty_t* t, i32 sig, const char* echo) {
    if (t->tio.c_lflag & ECHO) echo_str(t, echo);
    if (!(t->tio.c_lflag & NOFLSH)) {
        t->line_len = 0;
        t->q_count = 0;
    }
    if (t->fg_pgid > 0) signal_send_group(t->fg_pgid, sig);
    sched_wake_obj(t->queue);
}

static bool is_word_char(char c) {
    return c != ' ' && c != '\t';
}

static void canon_input(tty_t* t, char c) {
    const u8* cc = t->tio.c_cc;
    if (t->literal_next) {
        t->literal_next = false;
    } else if (c == cc[VERASE] || c == '\b') {
        if (t->line_len) echo_erase(t, t->line[--t->line_len]);
        return;
    } else if (c == cc[VKILL]) {
        while (t->line_len) echo_erase(t, t->line[--t->line_len]);
        return;
    } else if ((t->tio.c_lflag & IEXTEN) && c == cc[VWERASE]) {
        while (t->line_len && !is_word_char(t->line[t->line_len - 1])) echo_erase(t, t->line[--t->line_len]);
        while (t->line_len && is_word_char(t->line[t->line_len - 1])) echo_erase(t, t->line[--t->line_len]);
        return;
    } else if ((t->tio.c_lflag & IEXTEN) && c == cc[VREPRINT]) {
        echo_char(t, c);
        out_char(t, '\n');
        for (u32 i = 0; i < t->line_len; i++) echo_char(t, t->line[i]);
        return;
    } else if ((t->tio.c_lflag & IEXTEN) && c == cc[VLNEXT]) {
        t->literal_next = true;
        return;
    } else if (c == cc[VEOF]) {
        // Hand over what there is, without a newline; on an empty line
        // that is a zero-byte read: end of file.
        if (t->line_len == 0) t->eof_pending = true;
        enqueue(t, t->line, t->line_len);
        t->line_len = 0;
        sched_wake_obj(t->queue);
        return;
    } else if (c == '\n' || (cc[VEOL] && c == cc[VEOL]) || (cc[VEOL2] && c == cc[VEOL2])) {
        echo_char(t, c);
        t->line[t->line_len++] = c;
        enqueue(t, t->line, t->line_len);
        t->line_len = 0;
        return;
    }
    if (t->line_len >= TTY_LINE_MAX - 1) return;        // keep room for the newline
    t->line[t->line_len++] = c;
    echo_char(t, c);
}

void tty_input(tty_t* t, char c) {
    // Input translation.
    if (t->tio.c_iflag & ISTRIP) c &= 0x7F;
    if (c == '\r') {
        if (t->tio.c_iflag & IGNCR) return;
        if (t->tio.c_iflag & ICRNL) c = '\n';
    } else if (c == '\n' && (t->tio.c_iflag & INLCR)) {
        c = '\r';
    }

    if ((t->tio.c_lflag & ISIG) && !t->literal_next) {
        if (c == t->tio.c_cc[VINTR] && c) { raise_signal(t, SIGINT, "^C"); t->flush(t); return; }
        if (c == t->tio.c_cc[VQUIT] && c) { raise_signal(t, SIGQUIT, "^\\"); t->flush(t); return; }
        if (c == t->tio.c_cc[VSUSP] && c) { raise_signal(t, SIGTSTP, "^Z"); t->flush(t); return; }
    }

    if (t->tio.c_lflag & ICANON) {
        canon_input(t, c);
    } else {
        enqueue(t, &c, 1);
        echo_char(t, c);
    }
    t->flush(t);
}

void tty_input_str(tty_t* t, const char* s) {
    // Replies from the terminal itself: straight to readers, untranslated.
    u32 n = 0;
    while (s[n]) n++;
    enqueue(t, s, n);
}

static u64 dequeue(tty_t* t, char* out, u64 len, bool stop_at_newline) {
    u64 n = 0;
    while (n < len && t->q_count) {
        char c = t->queue[t->q_head];
        t->q_head = (t->q_head + 1) % TTY_QUEUE_MAX;
        t->q_count--;
        out[n++] = c;
        if (stop_at_newline && c == '\n') break;
    }
    return n;
}

bool tty_readable(tty_t* t) {
    if (t->hung_up) return true;
    if (t->tio.c_lflag & ICANON) return t->q_count || t->eof_pending;
    // Non-canonical with VMIN = 0 never blocks; otherwise data must be there.
    return t->q_count || t->tio.c_cc[VMIN] == 0;
}

u32 tty_bytes_ready(tty_t* t) {
    return t->q_count;
}

void tty_flush_input(tty_t* t) {
    t->q_count = 0;
    t->line_len = 0;
    t->eof_pending = false;
}

void tty_hangup(tty_t* t) {
    t->hung_up = true;
    sched_wake_obj(t->queue);
    sched_wake_obj(t);
    if (t->fg_pgid > 0) {
        signal_send_group(t->fg_pgid, SIGHUP);
        signal_send_group(t->fg_pgid, SIGCONT);
    }
    process_t* leader = t->sid > 0 ? proc_by_pid(t->sid) : NULL;
    if (leader && leader->state != PROC_ZOMBIE) signal_send(leader, SIGHUP);
}

// Block until there is input or `deadline` (in ticks; 0 = none) passes.
// False on a signal.
static bool wait_input(tty_t* t, u32 deadline) {
    if (!deadline) sched_block_on(t->queue);
    else sched_block_on_until(t->queue, deadline);
    return !signal_pending(proc_current());
}

i64 tty_read(tty_t* t, void* buf, u64 len, bool nonblock) {
    if (len == 0) return 0;
    t->flush(t);
    char* out = (char*)buf;

    if (t->tio.c_lflag & ICANON) {
        for (;;) {
            if (t->q_count) return (i64)dequeue(t, out, len, true);
            if (t->eof_pending) { t->eof_pending = false; return 0; }
            if (t->hung_up) return 0;
            if (nonblock) return -EAGAIN;
            if (!wait_input(t, 0)) return -EINTR;
        }
    }

    // Non-canonical: VMIN bytes, with VTIME (tenths of a second) as either
    // an overall timeout (VMIN = 0) or an inter-byte one (VMIN > 0).
    u32 vmin = t->tio.c_cc[VMIN], vtime = t->tio.c_cc[VTIME];
    u64 want = vmin < len ? vmin : len;
    u32 timeout = vtime * 25;                   // 250 Hz ticks
    if (!t->q_count && t->hung_up) return 0;
    if (nonblock && !t->q_count) return -EAGAIN;
    if (vmin == 0 && vtime == 0) return (i64)dequeue(t, out, len, false);

    u64 got = 0;
    u32 deadline = vtime ? (u32)ticks + timeout : 0;
    for (;;) {
        u64 before = got;
        got += dequeue(t, out + got, len - got, false);
        if (got >= len || (want && got >= want) || t->hung_up) return (i64)got;
        if (vtime) {
            if (got > before) deadline = (u32)ticks + timeout;   // restart the inter-byte timer
            bool timer_running = vmin == 0 || got > 0;
            if (timer_running && (i32)((u32)ticks - deadline) >= 0) return (i64)got;
            if (!wait_input(t, timer_running ? deadline : 0)) return got ? (i64)got : -EINTR;
        } else {
            if (!wait_input(t, 0)) return got ? (i64)got : -EINTR;
        }
    }
}

bool tty_kernel_getline(char* buf, u32 max) {
    tty_t* t = &tty_console;
    // Only take a complete line: look for its newline first.
    bool complete = false;
    for (u32 i = 0; i < t->q_count; i++) {
        if (t->queue[(t->q_head + i) % TTY_QUEUE_MAX] == '\n') { complete = true; break; }
    }
    if (!complete) {
        t->eof_pending = false;           // ^D means nothing to the kernel shell
        return false;
    }
    u64 n = dequeue(t, buf, max - 1, true);
    if (n && buf[n - 1] == '\n') n--;
    buf[n] = '\0';
    return true;
}

void tty_get_termios(tty_t* t, tty_termios_t* out) {
    *out = t->tio;
}

void tty_set_termios(tty_t* t, const tty_termios_t* in) {
    bool was_canon = t->tio.c_lflag & ICANON;
    t->tio = *in;
    // Leaving canonical mode: whatever was half-typed goes to readers as-is.
    if (was_canon && !(t->tio.c_lflag & ICANON) && t->line_len) {
        enqueue(t, t->line, t->line_len);
        t->line_len = 0;
    }
    sched_wake_obj(t->queue);             // VMIN may now be satisfied
}

u32 tty_get_mode(tty_t* t) {
    u32 m = 0;
    if (t->tio.c_lflag & ICANON) m |= TTY_MODE_CANON;
    if (t->tio.c_lflag & ECHO) m |= TTY_MODE_ECHO;
    if (t->tio.c_lflag & ISIG) m |= TTY_MODE_ISIG;
    return m;
}

void tty_set_mode(tty_t* t, u32 m) {
    tty_termios_t n = t->tio;
    n.c_lflag &= ~(ICANON | ECHO | ISIG);
    if (m & TTY_MODE_CANON) n.c_lflag |= ICANON;
    if (m & TTY_MODE_ECHO) n.c_lflag |= ECHO;
    if (m & TTY_MODE_ISIG) n.c_lflag |= ISIG;
    tty_set_termios(t, &n);
}

void tty_get_winsize(tty_t* t, u16* rows, u16* cols) {
    *rows = t->rows;
    *cols = t->cols;
}

void tty_set_winsize(tty_t* t, u16 rows, u16 cols) {
    if (t == &tty_console) return;          // the console's size is the screen's
    bool changed = rows != t->rows || cols != t->cols;
    t->rows = rows;
    t->cols = cols;
    if (changed && t->fg_pgid > 0) signal_send_group(t->fg_pgid, SIGWINCH);
}
