// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// Prime Meridian: the screen and keyboard as Linux programs (the X server)
// drive them.
//
//   /dev/fb0    fbdev: the mode (fixed: whatever the bootloader set) and
//               where the pixels are. Asked for another mode, we answer
//               with the current one, as efifb does.
//   the console KD_*: graphics mode (the console stops drawing) and the
//               keyboard's mode (characters, scancodes or keycodes);
//               VT_*: virtual terminals, of which there is exactly one.

#include <types.h>
#include <file.h>
#include <proc.h>
#include <usermode.h>
#include <memset.h>
#include <memcpy.h>
#include <errno.h>
#include <devfs.h>
#include <tty.h>
#include <fb.h>
#include <console.h>
#include <keyboard.h>
#include <klibc/string.h>
#include "linux.h"

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOPUT_VSCREENINFO 0x4601
#define FBIOGET_FSCREENINFO 0x4602
#define FBIOGETCMAP         0x4604
#define FBIOPUTCMAP         0x4605
#define FBIOPAN_DISPLAY     0x4606
#define FBIOBLANK           0x4611

#define KDGETLED   0x4B31
#define KDSETLED   0x4B32
#define KDGKBTYPE  0x4B33
#define KDSETMODE  0x4B3A
#define KDGETMODE  0x4B3B
#define KDGKBMODE  0x4B44
#define KDSKBMODE  0x4B45
#define KDGKBLED   0x4B64
#define KDSKBLED   0x4B65

#define VT_OPENQRY     0x5600
#define VT_GETMODE     0x5601
#define VT_SETMODE     0x5602
#define VT_GETSTATE    0x5603
#define VT_RELDISP     0x5605
#define VT_ACTIVATE    0x5606
#define VT_WAITACTIVE  0x5607
#define VT_DISALLOCATE 0x5608

#define KD_TEXT      0
#define KD_GRAPHICS  1
#define KB_101       0x02
#define K_UNICODE    3
#define K_OFF        4

typedef struct { u32 offset, length, msb_right; } l_fb_bitfield_t;

typedef struct {
    u32 xres, yres, xres_virtual, yres_virtual, xoffset, yoffset;
    u32 bits_per_pixel, grayscale;
    l_fb_bitfield_t red, green, blue, transp;
    u32 nonstd, activate, height, width, accel_flags;
    u32 pixclock, left_margin, right_margin, upper_margin, lower_margin;
    u32 hsync_len, vsync_len, sync, vmode, rotate, colorspace;
    u32 reserved[4];
} l_fb_var_t;

typedef struct {
    char id[16];
    u64 smem_start;
    u32 smem_len, type, type_aux, visual;
    u16 xpanstep, ypanstep, ywrapstep;
    u32 line_length;
    u64 mmio_start;
    u32 mmio_len, accel;
    u16 capabilities, reserved[2];
} l_fb_fix_t;

_Static_assert(sizeof(l_fb_var_t) == 160, "fb_var_screeninfo is 160 bytes");
_Static_assert(sizeof(l_fb_fix_t) == 80, "fb_fix_screeninfo is 80 bytes");

typedef struct { u8 mode, waitv; i16 relsig, acqsig, frsig; } l_vt_mode_t;
typedef struct { u16 v_active, v_signal, v_state; } l_vt_stat_t;

static void fill_var(l_fb_var_t* v) {
    const fb_info_t* fb = fb_info();
    memset(v, 0, sizeof(*v));
    v->xres = v->xres_virtual = fb->width;
    v->yres = v->yres_virtual = fb->height;
    v->bits_per_pixel = fb->bpp;
    v->red = (l_fb_bitfield_t){ fb->red_pos, fb->red_size, 0 };
    v->green = (l_fb_bitfield_t){ fb->green_pos, fb->green_size, 0 };
    v->blue = (l_fb_bitfield_t){ fb->blue_pos, fb->blue_size, 0 };
    v->height = v->width = 0xFFFFFFFF;           // size in mm: unknown
    v->pixclock = 10000;                         // made up, as efifb does
}

static i64 fb_ioctl(u32 req, u64 arg) {
    switch (req) {
        case FBIOGET_VSCREENINFO:
        case FBIOPUT_VSCREENINFO:                // the one mode there is
            if (!user_range_ok(arg, sizeof(l_fb_var_t), true)) return -EFAULT;
            fill_var((l_fb_var_t*)arg);
            return 0;
        case FBIOGET_FSCREENINFO: {
            if (!user_range_ok(arg, sizeof(l_fb_fix_t), true)) return -EFAULT;
            const fb_info_t* fb = fb_info();
            l_fb_fix_t* x = (l_fb_fix_t*)arg;
            memset(x, 0, sizeof(*x));
            strcpy(x->id, "kunixfb");
            x->smem_start = fb->phys;
            x->smem_len = (u32)fb->size;
            x->visual = 2;                       // FB_VISUAL_TRUECOLOR (type 0: packed pixels)
            x->line_length = fb->pitch;
            return 0;
        }
        case FBIOPAN_DISPLAY: {
            if (!user_range_ok(arg, sizeof(l_fb_var_t), false)) return -EFAULT;
            const l_fb_var_t* v = (const l_fb_var_t*)arg;
            return v->xoffset == 0 && v->yoffset == 0 ? 0 : -EINVAL;
        }
        case FBIOBLANK:
            return 0;
        case FBIOGETCMAP:
        case FBIOPUTCMAP:
            return -EINVAL;                      // true colour: no palette
        default:
            return -ENOTTY;
    }
}

static l_vt_mode_t vt_mode;
static u32 leds;

static i64 console_ioctl(u32 req, u64 arg) {
    switch (req) {
        case KDSETMODE:
            if (arg == KD_GRAPHICS) console_set_graphics(true, proc_current()->pid);
            else console_set_graphics(false, 0);
            return 0;
        case KDGETMODE:
            if (!user_range_ok(arg, 4, true)) return -EFAULT;
            *(i32*)arg = console_graphics() ? KD_GRAPHICS : KD_TEXT;
            return 0;
        case KDSKBMODE:
            if (arg > K_OFF) return -EINVAL;
            keyboard_set_mode(arg == K_UNICODE || arg == K_OFF ? KBD_XLATE : (int)arg);
            return 0;
        case KDGKBMODE:
            if (!user_range_ok(arg, 4, true)) return -EFAULT;
            *(i32*)arg = keyboard_mode();
            return 0;
        case KDGKBTYPE:
            if (!user_range_ok(arg, 1, true)) return -EFAULT;
            *(u8*)arg = KB_101;
            return 0;
        case KDGETLED:
        case KDGKBLED:
            if (!user_range_ok(arg, 1, true)) return -EFAULT;
            *(u8*)arg = (u8)leds;
            return 0;
        case KDSETLED:
        case KDSKBLED:
            leds = (u32)arg & 0xFF;
            return 0;

        case VT_OPENQRY:                         // the free one is the only one
            if (!user_range_ok(arg, 4, true)) return -EFAULT;
            *(i32*)arg = 1;
            return 0;
        case VT_GETSTATE: {
            if (!user_range_ok(arg, sizeof(l_vt_stat_t), true)) return -EFAULT;
            l_vt_stat_t* s = (l_vt_stat_t*)arg;
            s->v_active = 1;
            s->v_signal = 0;
            s->v_state = 0x3;                    // ttys 0 and 1 in use
            return 0;
        }
        case VT_GETMODE:
            if (!user_range_ok(arg, sizeof(l_vt_mode_t), true)) return -EFAULT;
            memcpy((void*)arg, &vt_mode, sizeof(vt_mode));
            return 0;
        case VT_SETMODE:
            // Nothing ever switches away, so the signals it asks for are
            // simply never sent.
            if (!user_range_ok(arg, sizeof(l_vt_mode_t), false)) return -EFAULT;
            memcpy(&vt_mode, (const void*)arg, sizeof(vt_mode));
            return 0;
        case VT_ACTIVATE:
        case VT_WAITACTIVE:
            return arg <= 1 ? 0 : -ENXIO;
        case VT_RELDISP:
        case VT_DISALLOCATE:
            return 0;
        default:
            return -ENOTTY;
    }
}

// From ioctl: true if req was one of ours (the result in *r).
bool pm_display_ioctl(file_t* f, u32 req, u64 arg, i64* r) {
    if (f->node && devfs_is_fb(f->node)) {
        *r = fb_ioctl(req, arg);
        return true;
    }
    if ((req >> 8) != 0x4B && (req >> 8) != 0x56) return false;
    if (file_tty(f) != &tty_console) {
        *r = -ENOTTY;                            // a pty is no console
        return true;
    }
    *r = console_ioctl(req, arg);
    return true;
}
