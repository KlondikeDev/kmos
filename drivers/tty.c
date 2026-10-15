// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <types.h>
#include <tty.h>
#include <console.h>
#include <keyboard.h>
#include <proc.h>
#include <signal.h>
#include <errno.h>

static u32 mode = TTY_MODE_CANON | TTY_MODE_ECHO | TTY_MODE_ISIG;
static i32 foreground_pgid = 0;

// The line being edited (canonical mode only).
static char line[TTY_LINE_MAX];
static u32  line_len = 0;

// Bytes ready for readers.
static char queue[TTY_QUEUE_MAX];
static u32  q_head = 0, q_count = 0;
static bool eof_pending = false;       // Ctrl+D on an empty line

void tty_init(void) {
    mode = TTY_MODE_CANON | TTY_MODE_ECHO | TTY_MODE_ISIG;
    line_len = q_head = q_count = 0;
    eof_pending = false;
}

static void echo(char c) {
    if (mode & TTY_MODE_ECHO) {
        terminal_putchar((unsigned char)c);
        terminal_flush();
    }
}

static void echo_str(const char* s) {
    while (*s) echo(*s++);
}

static void enqueue(const char* s, u32 n) {
    for (u32 i = 0; i < n && q_count < TTY_QUEUE_MAX; i++) {
        queue[(q_head + q_count) % TTY_QUEUE_MAX] = s[i];
        q_count++;
    }
    sched_wake_obj(&queue);
}

void tty_input(char c) {
    if ((mode & TTY_MODE_ISIG) && c == 0x03) {             // Ctrl+C
        echo_str("^C\n");
        line_len = 0;
        if (foreground_pgid > 0) signal_send_group(foreground_pgid, SIGINT);
        sched_wake_obj(&queue);
        return;
    }

    if (!(mode & TTY_MODE_CANON)) {
        enqueue(&c, 1);
        echo(c);
        return;
    }

    switch (c) {
        case 0x04:                                         // Ctrl+D
            if (line_len == 0) {
                eof_pending = true;
                sched_wake_obj(&queue);
            } else {
                enqueue(line, line_len);
                line_len = 0;
            }
            return;
        case '\b':
            if (line_len > 0) {
                line_len--;
                echo('\b');
            }
            return;
        case '\n':
            echo('\n');
            line[line_len++] = '\n';
            enqueue(line, line_len);
            line_len = 0;
            return;
        default:
            if ((unsigned char)c < 0x20 && c != '\t') return;  // other controls
            if (line_len >= TTY_LINE_MAX - 1) return;           // keep room for \n
            // The console refuses to backspace left of prompt_x, so pin it
            // where this line's typing started.
            if (line_len == 0) prompt_x = cursor_x;
            line[line_len++] = c;
            echo(c);
            return;
    }
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

i64 tty_read(void* buf, u64 len) {
    if (len == 0) return 0;
    terminal_flush();
    for (;;) {
        if (q_count) {
            return (i64)dequeue((char*)buf, len, mode & TTY_MODE_CANON);
        }
        if (eof_pending) {
            eof_pending = false;
            return 0;
        }
        sched_block_on(&queue);
        if (signal_pending(proc_current())) return -EINTR;
    }
}

i64 tty_write(const void* buf, u64 len) {
    const char* p = (const char*)buf;
    for (u64 i = 0; i < len; i++) terminal_putchar((unsigned char)p[i]);
    terminal_flush();
    return (i64)len;
}

bool tty_kernel_getline(char* buf, u32 max) {
    // Only take a complete line: look for its newline first.
    bool complete = false;
    for (u32 i = 0; i < q_count; i++) {
        if (queue[(q_head + i) % TTY_QUEUE_MAX] == '\n') { complete = true; break; }
    }
    if (!complete) {
        eof_pending = false;           // Ctrl+D means nothing to the kernel shell
        return false;
    }
    u64 n = dequeue(buf, max - 1, true);
    if (n && buf[n - 1] == '\n') n--;
    buf[n] = '\0';
    return true;
}

u32 tty_get_mode(void) {
    return mode;
}

void tty_set_mode(u32 m) {
    mode = m & (TTY_MODE_CANON | TTY_MODE_ECHO | TTY_MODE_ISIG);
    // Leaving canonical mode: whatever was half-typed goes to readers as-is.
    if (!(mode & TTY_MODE_CANON) && line_len) {
        enqueue(line, line_len);
        line_len = 0;
    }
}

i32 tty_get_foreground(void) {
    return foreground_pgid;
}

void tty_set_foreground(i32 pgid) {
    foreground_pgid = pgid;
}
