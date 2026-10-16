// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once
#include <types.h>

// The PS/2 mouse, as /dev/input/mice: 3-byte PS/2 packets.
void mouse_init(void);
void mouse_irq(void);                  // IRQ 12
i64 mouse_read(void* buf, u64 len, bool nonblock);
i64 mouse_write(const void* buf, u64 len);   // PS/2 commands, answered as a mouse would
bool mouse_readable(void);
