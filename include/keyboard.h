#pragma once
#include <types.h>

extern volatile uint32_t prompt_x;
extern char input_buffer[4096];
extern volatile int index;
void read_key();