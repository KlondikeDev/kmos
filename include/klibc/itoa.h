// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

void itoa(int value, char* buffer);
void utoa(u32 value, char* buffer, int base);
void utoa64(u64 value, char* buffer, int base);
void itoa64(i64 value, char* buffer);