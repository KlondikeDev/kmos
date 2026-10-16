// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>

// VT100/ANSI terminal emulation on the framebuffer console (TERM=linux).

void vt_init(void);                 // after terminal_init
void vt_putc(char c);               // interpret one byte (no screen flush)
void vt_write(const char* s, u64 n);
void vt_flush(void);                // draw the cursor and push to the screen
void vt_hide_cursor(void);          // before drawing over the cursor cell
