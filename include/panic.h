#pragma once

#include "idt.h"

void panic(const char* reason, registers_t* regs);