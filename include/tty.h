// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>

// The console terminal: keyboard in, framebuffer out, with a line
// discipline in between.
//
// In canonical ("cooked") mode -- the default -- keys are collected into a
// line and echoed; Backspace edits it, Enter hands it to readers (with its
// newline), Ctrl+D hands over a partial line or, on an empty line, makes
// the next read return 0 (end of file), and Ctrl+C throws the line away and
// sends SIGINT to the foreground process group.
//
// In raw mode every key goes to readers as soon as it is pressed, without
// echo or editing -- for programs (like a zsh line editor) that do their own.

#define TTY_LINE_MAX  256
#define TTY_QUEUE_MAX 1024

// Terminal mode flags, set through SYS_IOCTL.
#define TTY_MODE_CANON 0x1      // line editing; off = raw
#define TTY_MODE_ECHO  0x2      // echo typed characters
#define TTY_MODE_ISIG  0x4      // Ctrl+C sends SIGINT

void tty_init(void);

// A key arrived (called from the keyboard IRQ).
void tty_input(char c);

// Blocking read for processes. Returns bytes, 0 at end of file, or -EINTR.
i64 tty_read(void* buf, u64 len);
i64 tty_write(const void* buf, u64 len);

// For the kernel shell, which cannot block: copy out one complete line
// (without its newline) if there is one.
bool tty_kernel_getline(char* buf, u32 max);

u32 tty_get_mode(void);
void tty_set_mode(u32 mode);

i32 tty_get_foreground(void);
void tty_set_foreground(i32 pgid);
