// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <stdarg.h>

int init_serial();
void kserialout(const char* str);
void kserialputchar(char c);
void kserialoutf(const char* format, ...);
void vkserialoutf(const char* format, va_list args);
extern volatile bool faulty;

#define COM1 0x3f8          // COM1