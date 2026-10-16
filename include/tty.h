// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>

// Terminals: a POSIX line discipline over some output.
//
// Each tty_t is one terminal -- the console (keyboard in, framebuffer out
// through the VT emulator) or the slave side of a pseudo-terminal (input
// written by the master, output read by it). Its settings are a termios,
// in Linux's own layout and flag values, so Prime Meridian hands it to
// TCGETS/TCSETS unchanged. The defaults are the usual "sane" ones:
// canonical line editing with echo, ^C/^Z/^\ signals, CR->NL on input and
// NL->CR NL on output.
//
// Kunix's native TIOCGMODE/TIOCSMODE ioctls are a simpler view of the same
// state (just ICANON, ECHO and ISIG).

#define TTY_LINE_MAX  1024
#define TTY_QUEUE_MAX 4096

// Native mode flags (SYS_IOCTL TIOCGMODE/TIOCSMODE).
#define TTY_MODE_CANON 0x1      // line editing; off = raw
#define TTY_MODE_ECHO  0x2      // echo typed characters
#define TTY_MODE_ISIG  0x4      // ^C, ^Z, ^\ send signals

// struct termios as the Linux kernel ABI has it (TCGETS/TCSETS).
#define TTY_NCCS 19
typedef struct {
    u32 c_iflag, c_oflag, c_cflag, c_lflag;
    u8  c_line;
    u8  c_cc[TTY_NCCS];
} tty_termios_t;

// c_cc indices
#define VINTR    0
#define VQUIT    1
#define VERASE   2
#define VKILL    3
#define VEOF     4
#define VTIME    5
#define VMIN     6
#define VSTART   8
#define VSTOP    9
#define VSUSP    10
#define VEOL     11
#define VREPRINT 12
#define VWERASE  14
#define VLNEXT   15
#define VEOL2    16

// c_iflag
#define IGNBRK  0x001
#define BRKINT  0x002
#define ISTRIP  0x020
#define INLCR   0x040
#define IGNCR   0x080
#define ICRNL   0x100
#define IXON    0x400
#define IUTF8   0x4000
// c_oflag
#define OPOST   0x01
#define ONLCR   0x04
#define OCRNL   0x08
// c_cflag
#define B38400  0x00F
#define CS8     0x030
#define CREAD   0x080
#define HUPCL   0x400
// c_lflag
#define ISIG    0x0001
#define ICANON  0x0002
#define ECHO    0x0008
#define ECHOE   0x0010
#define ECHOK   0x0020
#define ECHONL  0x0040
#define NOFLSH  0x0080
#define TOSTOP  0x0100
#define ECHOCTL 0x0200
#define ECHOKE  0x0800
#define IEXTEN  0x8000

typedef struct tty {
    tty_termios_t tio;
    i32 fg_pgid;                  // the foreground process group
    i32 sid;                      // the session it is the controlling terminal of (0: none)
    u16 rows, cols;
    bool hung_up;                 // the other end is gone: reads give EOF, writes EIO

    // Where output (writes and echo) goes. A false return means "full":
    // writers wait for room, echo is dropped.
    bool (*output)(struct tty* t, char c);
    void (*flush)(struct tty* t);
    void* priv;

    // The line being edited (canonical mode only).
    char line[TTY_LINE_MAX];
    u32 line_len;
    bool literal_next;            // ^V: take the next byte as-is

    // Bytes ready for readers. In canonical mode only whole lines get here.
    char queue[TTY_QUEUE_MAX];
    u32 q_head, q_count;
    bool eof_pending;             // ^D on an empty line
} tty_t;

extern tty_t tty_console;

// Set a terminal to sane settings, empty, with this output.
void tty_setup(tty_t* t, bool (*output)(tty_t*, char), void (*flush)(tty_t*), void* priv);
void tty_init(void);              // the console

// A byte arrived (keyboard IRQ, or a write to a pty master), or a reply the
// terminal emulator owes the program (cursor position reports).
void tty_input(tty_t* t, char c);
void tty_input_str(tty_t* t, const char* s);

// Blocking read for processes: bytes, 0 at end of file, -EINTR, or -EAGAIN
// when nonblock is set and nothing is ready.
i64 tty_read(tty_t* t, void* buf, u64 len, bool nonblock);
i64 tty_write(tty_t* t, const void* buf, u64 len, bool nonblock);

// Could a read return right now (data, or end of file)?
bool tty_readable(tty_t* t);
u32  tty_bytes_ready(tty_t* t);  // FIONREAD
void tty_flush_input(tty_t* t);  // TCFLSH

// The other end went away (pty master closed): wake everyone, send the
// session SIGHUP.
void tty_hangup(tty_t* t);

// For the kernel shell, which cannot block: copy out one complete line
// (without its newline) if there is one.
bool tty_kernel_getline(char* buf, u32 max);

void tty_get_termios(tty_t* t, tty_termios_t* out);
void tty_set_termios(tty_t* t, const tty_termios_t* in);

u32 tty_get_mode(tty_t* t);
void tty_set_mode(tty_t* t, u32 mode);

// Size in character cells; setting it sends SIGWINCH to the foreground.
void tty_get_winsize(tty_t* t, u16* rows, u16* cols);
void tty_set_winsize(tty_t* t, u16 rows, u16 cols);

// ---- pseudo-terminals (drivers/pty.c) ----

#define PTY_MAX 16

struct file;
// Open /dev/ptmx: a new pair, returned as the master's open file.
i32 pty_open_master(struct file** out);
// The slave side of pty n, if it exists and has been unlocked.
tty_t* pty_slave(u32 n);
u32 pty_index(const tty_t* t);              // ~0 for the console
bool pty_exists(u32 n);
void pty_slave_opened(u32 n);               // devfs: someone opened /dev/pts/n
void pty_slave_closed(u32 n);               // ...and the last one closed it
// The terminal behind an open file (console, pts, or a ptmx master), or NULL.
tty_t* file_tty(struct file* f);
bool file_is_pty_master(struct file* f);
// TIOCGPTN / TIOCSPTLCK on a master.
i32 pty_master_number(struct file* f);
void pty_master_lock(struct file* f, bool locked);
