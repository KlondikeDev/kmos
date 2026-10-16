// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once
#include <types.h>

// The framebuffer the bootloader set up, as /dev/fb0 offers it to programs:
// one fixed mode, mapped straight into whoever mmaps the device.
typedef struct {
    u64 phys;                    // physical address of pixel (0, 0)
    u64 size;                    // bytes, a whole number of pages
    u32 width, height, pitch;    // pixels, pixels, bytes per line
    u32 bpp;
    u8 red_pos, red_size, green_pos, green_size, blue_pos, blue_size;
} fb_info_t;

const fb_info_t* fb_info(void);
i64 fb_read(u64 off, void* buf, u64 len);
i64 fb_write(u64 off, const void* buf, u64 len);
