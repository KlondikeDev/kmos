// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// /dev/fb0: the framebuffer itself. The console draws into it too, unless
// a program has asked for the screen (KD_GRAPHICS, see console.c).

#include <fb.h>
#include <console.h>
#include <memory.h>
#include <memcpy.h>
#include <errno.h>

#define PAGE 4096ULL

const fb_info_t* fb_info(void) {
    static fb_info_t info;
    if (!info.phys) {
        info.phys = framebuffer_addr - vmm_hhdm();      // Limine maps it in the direct map
        info.width = fb_width;
        info.height = fb_height;
        info.pitch = fb_pitch;
        info.bpp = fb_bpp;
        info.size = ((u64)fb_pitch * fb_height + PAGE - 1) & ~(PAGE - 1);
        info.red_pos = (u8)fb_red_field_pos;
        info.red_size = (u8)fb_red_mask_size;
        info.green_pos = (u8)fb_green_field_pos;
        info.green_size = (u8)fb_green_mask_size;
        info.blue_pos = (u8)fb_blue_field_pos;
        info.blue_size = (u8)fb_blue_mask_size;
    }
    return &info;
}

i64 fb_read(u64 off, void* buf, u64 len) {
    u64 size = (u64)fb_pitch * fb_height;
    if (off >= size) return 0;
    if (len > size - off) len = size - off;
    memcpy(buf, (const void*)(framebuffer_addr + off), len);
    return (i64)len;
}

i64 fb_write(u64 off, const void* buf, u64 len) {
    u64 size = (u64)fb_pitch * fb_height;
    if (off >= size) return len ? -ENOSPC : 0;          // as Linux's fb does
    if (len > size - off) len = size - off;
    memcpy((void*)(framebuffer_addr + off), buf, len);
    return (i64)len;
}
