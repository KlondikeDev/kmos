// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once
#include <types.h>

extern volatile uint32_t prompt_x;
void read_key();
void keyboard_init();

// What a key press puts on the console's input (Linux's KDSKBMODE): the
// usual characters and escape sequences, or for programs that do their
// own keymaps (the X server) the raw scancodes or Linux keycodes, with bit
// 7 set on release.
#define KBD_RAW       0
#define KBD_XLATE     1
#define KBD_MEDIUMRAW 2
void keyboard_set_mode(int mode);
int keyboard_mode(void);