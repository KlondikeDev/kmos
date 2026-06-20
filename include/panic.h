// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include "idt.h"

// Excuse me sir, I think there's someone you've confused me for!
void panic(const char* reason, registers_t* regs);