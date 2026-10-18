// This file has been generated in whole or in part by a Large Language Model
#pragma once
#include <types.h>
bool e1000_init(void);
bool e1000_link(void);
void e1000_mac(u8 out[6]);
bool e1000_send(const void* frame,u32 length);
// Copies one complete Ethernet frame; 0 if no frame, drops invalid fragments.
u32 e1000_receive(void* frame,u32 capacity);
