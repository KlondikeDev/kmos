// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>
#include <stdarg.h>

// Format into a buffer, like C's snprintf: %d %i %u %x %X %s %c %p %%, with
// the l/ll/z length modifiers, '-' and '0' flags and a field width. Always
// NUL-terminates (if cap > 0); returns the length the full output would have.
int ksnprintf(char* buf, u64 cap, const char* fmt, ...);
int kvsnprintf(char* buf, u64 cap, const char* fmt, va_list ap);
