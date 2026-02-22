// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once
#include <types.h>

extern volatile uint32_t prompt_x;
extern char input_buffer[4096];
extern volatile int index;
extern volatile bool command_ready;
void read_key();
void keyboard_init();