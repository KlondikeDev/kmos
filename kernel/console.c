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

// Initalizes framebuffer
void framebuffer_init(multiboot_info_t* mbi) {
    kserialout("Initalizing framebuffer\n");
    
    if (!mbi) {
        kserialout("ERROR: mbi is NULL!\n");
        while(1) { __asm__ volatile("hlt"); }
    }

    // Check if framebuffer info is present (bit 12)
    if (!(mbi->flags & (1 << 12))) {
        kserialout("ERROR: No framebuffer info from GRUB!\n");
        while(1) { __asm__ volatile("hlt"); }
    }
    
    char buf[20];
    utoa(mbi->flags, buf, 16);
    kserialout("mbi->flags: 0x");
    kserialout(buf);
    kserialout("\n");

    utoa(mbi->flags, buf, 16);
    kserialout("mbi->flags: 0x");
    kserialout(buf);
    kserialout("\n");
    
    // CRITICAL: Print actual framebuffer address
    utoa(mbi->framebuffer_addr, buf, 16);
    kserialout("Actual FB addr: 0x");
    kserialout(buf);
    kserialout("\n");
    
    utoa(mbi->framebuffer_width, buf, 10);
    kserialout("FB width: ");
    kserialout(buf);
    kserialout("\n");
    
    utoa(mbi->framebuffer_height, buf, 10);
    kserialout("FB height: ");
    kserialout(buf);
    kserialout("\n");
    
    // Setup definitions
    kb_lower             = mbi->mem_lower;
    kb_upper             = mbi->mem_upper;
    framebuffer_addr     = mbi->framebuffer_addr;
    fb_pitch             = mbi->framebuffer_pitch;
    fb_width             = mbi->framebuffer_width;
    fb_height            = mbi->framebuffer_height;
    fb_bpp               = mbi->framebuffer_bpp;
    fb_type              = mbi->framebuffer_type;
    fb_red_field_pos     = mbi->color_info[0];
    fb_red_mask_size     = mbi->color_info[1];
    fb_green_field_pos   = mbi->color_info[2];
    fb_green_mask_size   = mbi->color_info[3];
    fb_blue_field_pos    = mbi->color_info[4];
    fb_blue_mask_size    = mbi->color_info[5];
}

void terminal_init(void) {
    characters_wide = fb_width / FONT_WIDTH;
    characters_tall = fb_height / FONT_HEIGHT;

    cursor_x = 0;
    cursor_y = 0;

    terminal_clear();
}

void terminal_clear(void) {
    for(uint32_t y = 0; y < fb_height; y++){
        for(uint32_t x = 0; x < fb_width; x++){
            put_pixel(x, y, 0, 0, 0); // Fill background with black.
        }
    }
}

void put_pixel(uint32_t x,uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
    if (x >= fb_width || y >= fb_height){
        return;
    }

   uint32_t offset = (y * fb_pitch) + (x * (fb_bpp / 8));
    uint8_t *pixel = (uint8_t*)(framebuffer_addr + offset);

    // Handle different color orders
    if (fb_red_field_pos == 0) {
        // BGR order (red at position 0 means it's at byte 0, which is actually blue)
        pixel[0] = b;
        pixel[1] = g;
        pixel[2] = r;
        pixel[3] = 0xFF;
    } else {
        // RGB order
        pixel[0] = r;
        pixel[1] = g;
        pixel[2] = b;
        pixel[3] = 0xFF;
    }
}

void draw_char(uint32_t x,uint32_t y, unsigned char c,uint32_t fg_color,uint32_t bg_color) {
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
    uint64_t source = framebuffer_addr + (FONT_HEIGHT * fb_pitch);
    uint64_t dest = framebuffer_addr;
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
}