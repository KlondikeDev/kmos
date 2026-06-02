// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once
#include <types.h>

extern uint32_t cursor_x;
extern uint32_t cursor_y;

extern uint32_t kb_lower;
extern uint32_t kb_upper;
extern uint32_t fb_addr_low;
extern uint32_t fb_addr_high;
extern volatile uint64_t framebuffer_addr;
extern volatile uint32_t fb_pitch;
extern volatile uint32_t fb_width;
extern volatile uint32_t fb_height;
extern uint16_t fb_bpp;
extern uint16_t fb_type;
extern uint16_t fb_red_field_pos;
extern uint16_t fb_red_mask_size;
extern uint16_t fb_green_field_pos;
extern uint16_t fb_green_mask_size;
extern uint16_t fb_blue_field_pos;
extern uint16_t fb_blue_mask_size;
extern uint32_t cursor_x;
extern uint32_t cursor_y;
extern uint32_t characters_wide;
extern uint32_t characters_tall;


// Functions
void framebuffer_init(void);
void put_pixel(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b);
void draw_char(uint32_t x, uint32_t y, unsigned char c, uint32_t fg_color, uint32_t bg_color);
void terminal_scroll(void);
void terminal_init(void);
void terminal_puts(const char* str);
void terminal_putchar(unsigned char c);
void terminal_clear(void);
void draw_cursor(void);
void terminal_printf(uint32_t fg, uint32_t bg, const char* format, ...);
void print_padded(const char* str, int width, char pad_char);
void terminal_flush(void);

#define WHITE   0xFFFFFF
#define BLACK   0x000000
#define RED     0xFF0000
#define ORANGE  0xFFA500
#define YELLOW  0xFFFF00
#define GREEN   0x008000
#define CYAN    0x00FFFF
#define BLUE    0x0000FF
#define PURPLE  0x800080
#define MAGENTA 0xFF00FF
#define PINK    0xFFC0CB
#define GREY    0x808080
#define SILVER  0xC0C0C0