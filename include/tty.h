// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>

// The console terminal: keyboard in, framebuffer (through the VT emulator)
// out, with a POSIX line discipline in between.
//
// Its settings are a termios, in Linux's own layout and flag values, so
// Prime Meridian hands it to TCGETS/TCSETS unchanged. The defaults are the
// usual "sane" ones: canonical line editing with echo, ^C/^Z/^\ signals,
// CR->NL on input and NL->CR NL on output.
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

void tty_init(void);

// A byte arrived from the keyboard (IRQ context), or a reply the terminal
// emulator owes the program (cursor position reports and the like).
void tty_input(char c);
void tty_input_str(const char* s);

// Blocking read for processes: bytes, 0 at end of file, -EINTR, or -EAGAIN
// when nonblock is set and nothing is ready.
i64 tty_read(void* buf, u64 len, bool nonblock);
i64 tty_write(const void* buf, u64 len);

// Could a read return right now (data, or end of file)?
bool tty_readable(void);
u32  tty_bytes_ready(void);      // FIONREAD
void tty_flush_input(void);      // TCFLSH

// For the kernel shell, which cannot block: copy out one complete line
// (without its newline) if there is one.
bool tty_kernel_getline(char* buf, u32 max);

void tty_get_termios(tty_termios_t* t);
void tty_set_termios(const tty_termios_t* t);

u32 tty_get_mode(void);
void tty_set_mode(u32 mode);

i32 tty_get_foreground(void);
void tty_set_foreground(i32 pgid);

// Size in character cells.
void tty_get_winsize(u16* rows, u16* cols);
