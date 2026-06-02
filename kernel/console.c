// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <console.h>
#include <types.h>
#include <serial.h>
#include <font.h>
#include <memcpy.h>
#include <stdarg.h>
#include <itoa.h>
#include <string.h>
#include <keyboard.h>
#include <memset.h> 
#include <limine.h>

extern volatile struct limine_framebuffer_request fb_request;

// Defined as extern in console.h
uint32_t kb_lower;
uint32_t kb_upper;
uint32_t fb_addr_low;
uint32_t fb_addr_high;
volatile uint64_t framebuffer_addr;
volatile uint32_t fb_pitch;
volatile uint32_t fb_width;
volatile uint32_t fb_height;
uint16_t fb_bpp;
uint16_t fb_type;
uint16_t fb_red_field_pos;
uint16_t fb_red_mask_size;
uint16_t fb_green_field_pos;
uint16_t fb_green_mask_size;
uint16_t fb_blue_field_pos;
uint16_t fb_blue_mask_size;
uint32_t cursor_x;
uint32_t cursor_y;
uint32_t characters_wide;
uint32_t characters_tall;
uint32_t color_fg = WHITE;
uint32_t color_bg = BLACK;



#define FONT_HEIGHT 16
#define FONT_WIDTH 8

static uint8_t shadow[1920 * 1080 * 4];
static uint32_t dirty_top = UINT32_MAX;
static uint32_t dirty_bottom = 0;

// Initalizes framebuffer
void framebuffer_init(void) {
    if (!fb_request.response || fb_request.response->framebuffer_count < 1) {
        kserialout("ERROR: No framebuffer from Limine!\n");
        while(1) { __asm__ volatile("hlt"); }
    }

    struct limine_framebuffer *fb = fb_request.response->framebuffers[0];

    framebuffer_addr     = (uint64_t)fb->address;
    fb_pitch             = fb->pitch;
    fb_width             = fb->width;
    fb_height            = fb->height;
    fb_bpp               = fb->bpp;
    fb_red_field_pos     = fb->red_mask_shift;
    fb_green_field_pos   = fb->green_mask_shift;
    fb_blue_field_pos    = fb->blue_mask_shift;
    fb_red_mask_size     = fb->red_mask_size;
    fb_green_mask_size   = fb->green_mask_size;
    fb_blue_mask_size    = fb->blue_mask_size;
}

static void mark_dirty(uint32_t y_pixel) {
    if (y_pixel < dirty_top) dirty_top = y_pixel;
    if (y_pixel + FONT_HEIGHT > dirty_bottom) dirty_bottom = y_pixel + FONT_HEIGHT;
}


void terminal_init(void) {
    characters_wide = fb_width / FONT_WIDTH;
    characters_tall = fb_height / FONT_HEIGHT;

    cursor_x = 0;
    cursor_y = 0;

    terminal_clear();
}

void terminal_clear(void) {
    memset(shadow, 0, fb_height * fb_pitch);
    dirty_top = 0;
    dirty_bottom = fb_height;
    terminal_flush();
}


void put_pixel(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
    if (x >= fb_width || y >= fb_height) return;
    uint32_t offset = (y * fb_pitch) + (x * (fb_bpp / 8));
    uint8_t *pixel = (uint8_t*)(shadow + offset);

    // Handle different color orders
    if (fb_red_field_pos == 0) {
        // RGB: red at bit 0
        pixel[0] = r;
        pixel[1] = g;
        pixel[2] = b;
        pixel[3] = 0xFF;
    } else {
        // BGR: red at bit 16
        pixel[0] = b;
        pixel[1] = g;
        pixel[2] = r;
        pixel[3] = 0xFF;
    }
}

void draw_char(uint32_t x,uint32_t y, unsigned char c,uint32_t fg_color,uint32_t bg_color) {
    mark_dirty(y);
    uint8_t *glyph = font8x16[(uint16_t)c];  // Get character's bitmap
    
    for (int row = 0; row < FONT_HEIGHT; row++) {
        uint16_t row_data = glyph[row];
        
        for (int col = 0; col < FONT_WIDTH; col++) {
            // Extract bit: shift right by (7-col), mask with 1
            uint16_t bit = (row_data >> (7 - col)) & 1;
            
            if (bit) {
                // Draw foreground
                put_pixel(x + col, y + row, (fg_color >> 16) & 0xFF, (fg_color >> 8) & 0xFF, fg_color & 0xFF);
            } else {
                // Draw background
                put_pixel(x + col, y + row, (bg_color >> 16) & 0xFF, (bg_color >> 8) & 0xFF, bg_color & 0xFF);
            }
        }
    }
}

void terminal_putchar(unsigned char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;

        if(cursor_y >= characters_tall) {
            terminal_scroll();
            cursor_y = characters_tall - 1;
        }

        return;
    }

    if (c == '\t') {
        for (int i = 0; i < 4; i++){
            terminal_putchar(' ');
        }

        return;
    }

    if (c == '\r') {
        cursor_x = 0;
        return;
    }

    if (c == '\b') {
        if (cursor_x > prompt_x) {
            cursor_x--;
        
            uint32_t pixel_x = cursor_x * FONT_WIDTH;
            uint32_t pixel_y = cursor_y * FONT_HEIGHT;
            draw_char(pixel_x, pixel_y, ' ', color_fg, color_bg);
        }
        return;
    }

    uint32_t pixel_x = cursor_x * FONT_WIDTH;
    uint32_t pixel_y = cursor_y * FONT_HEIGHT;
    draw_char(pixel_x, pixel_y, c, color_fg, color_bg);

    cursor_x++;
    if (cursor_x >= characters_wide) {
        cursor_x = 0;

        cursor_y++;

        if (cursor_y >= characters_tall) {
            terminal_scroll();
            cursor_y = characters_tall - 1;
        }
    }
}

void terminal_puts(const char* str) {
    while (*str) {
        terminal_putchar(*str);
        str++;
    }
}

void terminal_scroll(void) {
    __asm__ volatile("cli");
    uint64_t source = (uint64_t)shadow + (FONT_HEIGHT * fb_pitch);
    uint64_t dest = (uint64_t)shadow;
    uint32_t size = (characters_tall - 1) * FONT_HEIGHT * fb_pitch;
    
    memcpy((void*)dest, (void*)source, size);

    uint32_t old_x = cursor_x;
    for (uint32_t i = 0; i < characters_wide; i++) {
        uint32_t pixel_x = i * FONT_WIDTH;
        uint32_t pixel_y = (characters_tall - 1) * FONT_HEIGHT;
        draw_char(pixel_x, pixel_y, ' ', color_bg, color_bg);
    }

    cursor_x = old_x;
    cursor_y = characters_tall - 1;
    dirty_top = 0;
    dirty_bottom = fb_height;
    terminal_flush();
    __asm__ volatile("sti");
}

void draw_cursor(void) {
   uint32_t pixel_x = cursor_x * FONT_WIDTH;
   uint32_t pixel_y = cursor_y * FONT_HEIGHT;

    for (uint32_t i = 0; i < FONT_HEIGHT; i++) {
        put_pixel(pixel_x, pixel_y + i, 255, 255, 255);
    }
}

void print_padded(const char* str, int width, char pad_char) {
    int len = strlen(str);
    int padding = width - len;
    
    if (padding > 0) {
        for (int i = 0; i < padding; i++) {
            terminal_putchar(pad_char);
        }
    }
    
    terminal_puts(str);
}

void terminal_printf(uint32_t fg, uint32_t bg, const char* format, ...) {
    uint32_t old_bg = color_bg;
    uint32_t old_fg = color_fg;
    color_fg = fg;
    color_bg = bg;
    
    va_list args;
    va_start(args, format);

    while (*format) {
    if (*format == '%') {
        format++;
        
        if (*format == '-') {
            format++;
        }
        
        char pad_char = ' ';
        if (*format == '0') {
            pad_char = '0';
            format++;
        }
            
            int width = 0;
            while (*format >= '0' && *format <= '9') {
                width = width * 10 + (*format - '0');
                format++;
            }

            int is_long = 0;
            if (*format == 'l') {
                is_long = 1;
                format++;
            }

            switch (*format) {
                case 'd': {
                    char buffer[100];
                    if (is_long) {
                        i64 val = va_arg(args, i64);
                        itoa64(val, buffer);
                    } else {
                        int32_t val = va_arg(args, int);
                        itoa(val, buffer);
                    }
                    print_padded(buffer, width, pad_char);
                    break;
                }
                case 'u': {
                    char buffer[100];
                    if (is_long) {
                        u64 val = va_arg(args, u64);
                        utoa64(val, buffer, 10);
                    } else {
                        uint32_t val = va_arg(args, unsigned int);
                        utoa(val, buffer, 10);
                    }
                    print_padded(buffer, width, pad_char);
                    break;
                }
                case 'x': {
                    char buffer[100];
                    if (is_long) {
                        u64 val = va_arg(args, u64);
                        utoa64(val, buffer, 16);
                    } else {
                        uint32_t val = va_arg(args, unsigned int);
                        utoa(val, buffer, 16);
                    }
                    print_padded(buffer, width, pad_char);
                    break;
                }
                case 's': {
                    char* str = va_arg(args, char*);
                    print_padded(str, width, ' ');
                    break;
                }
                case '%': {
                    terminal_putchar('%');
                    break;
                }
                default: {
                    terminal_putchar('%');
                    terminal_putchar(*format);
                }
            }
        } else {
            terminal_putchar(*format);
        }
        format++;
    }

    va_end(args);
    color_bg = old_bg;
    color_fg = old_fg;
    terminal_flush();
}

void terminal_flush(void) {
    __asm__ volatile("cli");
    if (dirty_top == UINT32_MAX) {
        __asm__ volatile("sti");
        return;
    }
    uint32_t offset = dirty_top * fb_pitch;
    uint32_t size = (dirty_bottom - dirty_top) * fb_pitch;
    memcpy((void*)(framebuffer_addr + offset), shadow + offset, size);
    dirty_top = UINT32_MAX;
    dirty_bottom = 0;
    __asm__ volatile("sti");
}