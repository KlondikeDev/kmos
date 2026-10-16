// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// The console's line discipline, driven by a termios.
//
// Input (tty_input, from the keyboard IRQ) is translated (ICRNL, ...), then
// either checked for signal characters (ISIG), edited into a line
// (ICANON), or queued for readers byte by byte. Readers block in tty_read.
// Output -- program writes and echo alike -- goes through OPOST
// processing to the VT emulator, which draws on the framebuffer console.
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

static tty_termios_t tio;
static i32 foreground_pgid = 0;

// The line being edited (canonical mode only).
static char line[TTY_LINE_MAX];
static u32  line_len = 0;
static bool literal_next = false;      // ^V: take the next byte as-is

// Bytes ready for readers. In canonical mode only whole lines get here.
static char queue[TTY_QUEUE_MAX];
static u32  q_head = 0, q_count = 0;
static bool eof_pending = false;       // ^D on an empty line

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

void tty_init(void) {
    tio = sane;
    line_len = q_head = q_count = 0;
    eof_pending = literal_next = false;
}

// ---- output ----

static void out_char(char c) {
    if ((tio.c_oflag & OPOST) && c == '\n' && (tio.c_oflag & ONLCR)) vt_putc('\r');
    else if ((tio.c_oflag & OPOST) && c == '\r' && (tio.c_oflag & OCRNL)) c = '\n';
    vt_putc(c);
}

i64 tty_write(const void* buf, u64 len) {
    const char* p = (const char*)buf;
    for (u64 i = 0; i < len; i++) out_char(p[i]);
    vt_flush();
    return (i64)len;
}

// Echo one input byte the way a terminal shows it: control characters as
// ^X when ECHOCTL is set.
static void echo_char(char c) {
    if (!(tio.c_lflag & ECHO)) {
        if (c == '\n' && (tio.c_lflag & ECHONL)) out_char('\n');
        return;
    }
    u8 u = (u8)c;
    if ((tio.c_lflag & ECHOCTL) && (u < 0x20 || u == 0x7F) && c != '\n' && c != '\t') {
        out_char('^');
        out_char(u == 0x7F ? '?' : (char)(u + '@'));
    } else {
        out_char(c);
    }
}

static void echo_str(const char* s) {
    while (*s) echo_char(*s++);
}

// Visually remove the last character of the line being edited.
static void echo_erase(char gone) {
    if (!(tio.c_lflag & ECHO)) return;
    u32 cols = ((tio.c_lflag & ECHOCTL) && ((u8)gone < 0x20 || (u8)gone == 0x7F) && gone != '\t') ? 2 : 1;
    for (u32 i = 0; i < cols; i++) {
        if (tio.c_lflag & ECHOE) { out_char('\b'); out_char(' '); out_char('\b'); }
        else out_char('\b');
    }
}

// ---- input ----

static void enqueue(const char* s, u32 n) {
    for (u32 i = 0; i < n && q_count < TTY_QUEUE_MAX; i++) {
        queue[(q_head + q_count) % TTY_QUEUE_MAX] = s[i];
        q_count++;
    }
    sched_wake_obj(&queue);
}

static void raise_signal(i32 sig, const char* echo) {
    if (tio.c_lflag & ECHO) echo_str(echo);
    if (!(tio.c_lflag & NOFLSH)) {
        line_len = 0;
        q_count = 0;
    }
    if (foreground_pgid > 0) signal_send_group(foreground_pgid, sig);
    sched_wake_obj(&queue);
}

static bool is_word_char(char c) {
    return c != ' ' && c != '\t';
}

static void canon_input(char c) {
    const u8* cc = tio.c_cc;
    if (literal_next) {
        literal_next = false;
    } else if (c == cc[VERASE] || c == '\b') {
        if (line_len) echo_erase(line[--line_len]);
        return;
    } else if (c == cc[VKILL]) {
        while (line_len) echo_erase(line[--line_len]);
        return;
    } else if ((tio.c_lflag & IEXTEN) && c == cc[VWERASE]) {
        while (line_len && !is_word_char(line[line_len - 1])) echo_erase(line[--line_len]);
        while (line_len && is_word_char(line[line_len - 1])) echo_erase(line[--line_len]);
        return;
    } else if ((tio.c_lflag & IEXTEN) && c == cc[VREPRINT]) {
        echo_char(c);
        out_char('\n');
        for (u32 i = 0; i < line_len; i++) echo_char(line[i]);
        return;
    } else if ((tio.c_lflag & IEXTEN) && c == cc[VLNEXT]) {
        literal_next = true;
        return;
    } else if (c == cc[VEOF]) {
        // Hand over what there is, without a newline; on an empty line
        // that is a zero-byte read: end of file.
        if (line_len == 0) eof_pending = true;
        enqueue(line, line_len);
        line_len = 0;
        sched_wake_obj(&queue);
        return;
    } else if (c == '\n' || (cc[VEOL] && c == cc[VEOL]) || (cc[VEOL2] && c == cc[VEOL2])) {
        echo_char(c);
        line[line_len++] = c;
        enqueue(line, line_len);
        line_len = 0;
        return;
    }
    if (line_len >= TTY_LINE_MAX - 1) return;           // keep room for the newline
    line[line_len++] = c;
    echo_char(c);
}

void tty_input(char c) {
    // Input translation.
    if (tio.c_iflag & ISTRIP) c &= 0x7F;
    if (c == '\r') {
        if (tio.c_iflag & IGNCR) return;
        if (tio.c_iflag & ICRNL) c = '\n';
    } else if (c == '\n' && (tio.c_iflag & INLCR)) {
        c = '\r';
    }

    if ((tio.c_lflag & ISIG) && !literal_next) {
        if (c == tio.c_cc[VINTR] && c) { raise_signal(SIGINT, "^C"); return; }
        if (c == tio.c_cc[VQUIT] && c) { raise_signal(SIGQUIT, "^\\"); return; }
        if (c == tio.c_cc[VSUSP] && c) { raise_signal(SIGTSTP, "^Z"); return; }
    }

    if (tio.c_lflag & ICANON) {
        canon_input(c);
    } else {
        enqueue(&c, 1);
        echo_char(c);
    }
    vt_flush();
}

void tty_input_str(const char* s) {
    // Replies from the terminal itself: straight to readers, untranslated.
    u32 n = 0;
    while (s[n]) n++;
    enqueue(s, n);
}

static u64 dequeue(char* out, u64 len, bool stop_at_newline) {
    u64 n = 0;
    while (n < len && q_count) {
        char c = queue[q_head];
        q_head = (q_head + 1) % TTY_QUEUE_MAX;
        q_count--;
        out[n++] = c;
        if (stop_at_newline && c == '\n') break;
    }
    return n;
}

bool tty_readable(void) {
    if (tio.c_lflag & ICANON) return q_count || eof_pending;
    // Non-canonical with VMIN = 0 never blocks; otherwise data must be there.
    return q_count || tio.c_cc[VMIN] == 0;
}

u32 tty_bytes_ready(void) {
    return q_count;
}

void tty_flush_input(void) {
    q_count = 0;
    line_len = 0;
    eof_pending = false;
}

// Block until there is input or `deadline` (in ticks; 0 = none) passes.
// False on a signal.
static bool wait_input(u32 deadline) {
    if (!deadline) {
        sched_block_on(&queue);
    } else {
        sched_sleep(1);                // no timed wait queues yet: poll per tick
    }
    return !signal_pending(proc_current());
}

i64 tty_read(void* buf, u64 len, bool nonblock) {
    if (len == 0) return 0;
    vt_flush();
    char* out = (char*)buf;

    if (tio.c_lflag & ICANON) {
        for (;;) {
            if (q_count) return (i64)dequeue(out, len, true);
            if (eof_pending) { eof_pending = false; return 0; }
            if (nonblock) return -EAGAIN;
            if (!wait_input(0)) return -EINTR;
        }
    }

    // Non-canonical: VMIN bytes, with VTIME (tenths of a second) as either
    // an overall timeout (VMIN = 0) or an inter-byte one (VMIN > 0).
    u32 vmin = tio.c_cc[VMIN], vtime = tio.c_cc[VTIME];
    u64 want = vmin < len ? vmin : len;
    u32 timeout = vtime * 25;          // 250 Hz ticks
    if (nonblock && !q_count) return -EAGAIN;
    if (vmin == 0 && vtime == 0) return (i64)dequeue(out, len, false);

    u64 got = 0;
    u32 deadline = vtime ? (u32)ticks + timeout : 0;
    for (;;) {
        u64 before = got;
        got += dequeue(out + got, len - got, false);
        if (got >= len || (want && got >= want)) return (i64)got;
        if (vtime) {
            if (got > before) deadline = (u32)ticks + timeout;   // restart the inter-byte timer
            bool timer_running = vmin == 0 || got > 0;
            if (timer_running && (i32)((u32)ticks - deadline) >= 0) return (i64)got;
            if (!wait_input(timer_running ? deadline : 0)) return got ? (i64)got : -EINTR;
        } else {
            if (!wait_input(0)) return got ? (i64)got : -EINTR;
        }
    }
}

bool tty_kernel_getline(char* buf, u32 max) {
    // Only take a complete line: look for its newline first.
    bool complete = false;
    for (u32 i = 0; i < q_count; i++) {
        if (queue[(q_head + i) % TTY_QUEUE_MAX] == '\n') { complete = true; break; }
    }
    if (!complete) {
        eof_pending = false;           // ^D means nothing to the kernel shell
        return false;
    }
    u64 n = dequeue(buf, max - 1, true);
    if (n && buf[n - 1] == '\n') n--;
    buf[n] = '\0';
    return true;
}

void tty_get_termios(tty_termios_t* t) {
    *t = tio;
}

void tty_set_termios(const tty_termios_t* t) {
    bool was_canon = tio.c_lflag & ICANON;
    tio = *t;
    // Leaving canonical mode: whatever was half-typed goes to readers as-is.
    if (was_canon && !(tio.c_lflag & ICANON) && line_len) {
        enqueue(line, line_len);
        line_len = 0;
    }
    sched_wake_obj(&queue);            // VMIN may now be satisfied
}

u32 tty_get_mode(void) {
    u32 m = 0;
    if (tio.c_lflag & ICANON) m |= TTY_MODE_CANON;
    if (tio.c_lflag & ECHO) m |= TTY_MODE_ECHO;
    if (tio.c_lflag & ISIG) m |= TTY_MODE_ISIG;
    return m;
}

void tty_set_mode(u32 m) {
    tty_termios_t t = tio;
    t.c_lflag &= ~(ICANON | ECHO | ISIG);
    if (m & TTY_MODE_CANON) t.c_lflag |= ICANON;
    if (m & TTY_MODE_ECHO) t.c_lflag |= ECHO;
    if (m & TTY_MODE_ISIG) t.c_lflag |= ISIG;
    tty_set_termios(&t);
}

i32 tty_get_foreground(void) {
    return foreground_pgid;
}

void tty_set_foreground(i32 pgid) {
    foreground_pgid = pgid;
}

void tty_get_winsize(u16* rows, u16* cols) {
    *rows = (u16)characters_tall;
    *cols = (u16)characters_wide;
}
