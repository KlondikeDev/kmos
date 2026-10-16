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
#include <io.h>
#include <limine.h>
#include <vt.h>

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

    // The shadow buffer is statically sized; on a framebuffer larger than
    // 1920x1080x32 clamp the drawable height rather than overflow .bss.
    if ((uint64_t)fb_height * fb_pitch > sizeof(shadow)) {
        kserialout("WARNING: framebuffer exceeds console shadow buffer, clamping height\n");
        fb_height = sizeof(shadow) / fb_pitch;
    }
}

static void mark_dirty(uint32_t y_pixel) {
    if (y_pixel < dirty_top) dirty_top = y_pixel;
    if (y_pixel + FONT_HEIGHT > dirty_bottom) dirty_bottom = y_pixel + FONT_HEIGHT;
}

// The shadow buffer is a ring of text rows: scrolling moves which row is on
// top (ring_top) instead of moving four megabytes of pixels, and the real
// framebuffer is brought up to date once per flush, not once per line.
static uint32_t ring_top;

static uint32_t row_bytes(void) {
    return FONT_HEIGHT * fb_pitch;
}

// Where pixel line `line` (0-15) of text row `row` lives in the shadow.
static uint8_t* row_line(uint32_t row, uint32_t line) {
    uint32_t rows = characters_tall ? characters_tall : 1;
    return shadow + ((row + ring_top) % rows) * row_bytes() + line * fb_pitch;
}

// A colour (0xRRGGBB) as the framebuffer stores a 32-bit pixel.
static uint32_t pack(uint32_t rgb) {
    uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return 0xFF000000u | (fb_red_field_pos == 0 ? (b << 16 | g << 8 | r) : (r << 16 | g << 8 | b));
}

static void fill32(uint32_t* p, uint32_t value, uint64_t count) {
    __asm__ volatile("rep stosl" : "+D"(p), "+c"(count) : "a"(value) : "memory");
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
    ring_top = 0;
    dirty_top = 0;
    dirty_bottom = fb_height;
    terminal_flush();
}


void put_pixel(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
    if (x >= fb_width || y >= characters_tall * FONT_HEIGHT) return;
    uint8_t *pixel = row_line(y / FONT_HEIGHT, y % FONT_HEIGHT) + x * (fb_bpp / 8);

    // Handle different color orders
    if (fb_red_field_pos == 0) {
        // RGB: red at bit 0
        pixel[0] = r;
        pixel[1] = g;
        pixel[2] = b;
        if (fb_bpp == 32) pixel[3] = 0xFF;
    } else {
        // BGR: red at bit 16
        pixel[0] = b;
        pixel[1] = g;
        pixel[2] = r;
        if (fb_bpp == 32) pixel[3] = 0xFF;
    }
}

void draw_char(uint32_t x,uint32_t y, unsigned char c,uint32_t fg_color,uint32_t bg_color) {
    mark_dirty(y);
    uint8_t *glyph = font8x16[(uint16_t)c];  // Get character's bitmap
    if (fb_bpp == 32 && y % FONT_HEIGHT == 0 && x + FONT_WIDTH <= fb_width &&
        y / FONT_HEIGHT < characters_tall) {
        // The usual case: whole 32-bit pixels, a row of the glyph at a time.
        uint32_t fg = pack(fg_color), bg = pack(bg_color);
        for (int row = 0; row < FONT_HEIGHT; row++) {
            uint32_t* p = (uint32_t*)(row_line(y / FONT_HEIGHT, (uint32_t)row) + x * 4);
            uint8_t bits = glyph[row];
            for (int col = 0; col < FONT_WIDTH; col++) p[col] = (bits & (0x80 >> col)) ? fg : bg;
        }
        return;
    }
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
    vt_hide_cursor();                // the VT's cursor may be drawn on this cell
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
    uint64_t irq = irq_save();
    console_scroll(1, color_bg);
    cursor_y = characters_tall - 1;
    irq_restore(irq);
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

            if (*format == '\0') {   // format ends in '%': don't run past the terminator
                terminal_putchar('%');
                break;
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

// KD_GRAPHICS: a program (the X server) owns the screen. The console goes
// on drawing into its shadow, just not onto the screen, so that handing
// the screen back shows the text as it now stands.
static bool graphics;
static int32_t graphics_owner;

void terminal_flush(void) {
    uint64_t irq = irq_save();
    if (dirty_top == UINT32_MAX) {
        irq_restore(irq);
        return;
    }
    if (graphics) {
        dirty_top = UINT32_MAX;
        dirty_bottom = 0;
        irq_restore(irq);
        return;
    }
    // Text row by text row, out of the ring and into place.
    uint32_t first = dirty_top / FONT_HEIGHT, last = (dirty_bottom - 1) / FONT_HEIGHT;
    if (last >= characters_tall) last = characters_tall - 1;
    for (uint32_t row = first; row <= last; row++) {
        memcpy((void*)(framebuffer_addr + row * row_bytes()), row_line(row, 0), row_bytes());
    }
    dirty_top = UINT32_MAX;
    dirty_bottom = 0;
    irq_restore(irq);
}

void console_set_graphics(bool on, int32_t owner) {
    uint64_t irq = irq_save();
    bool was = graphics;
    graphics = on;
    graphics_owner = on ? owner : 0;
    if (was && !on) {
        dirty_top = 0;                 // the whole screen is ours again
        dirty_bottom = characters_tall * FONT_HEIGHT;
        terminal_flush();
    }
    irq_restore(irq);
}

bool console_graphics(void) {
    return graphics;
}

// A process is gone: if it had the screen, the console takes it back (and
// the keyboard its usual mode), so that a crashed X server does not leave
// a dead screen behind.
void console_process_gone(int32_t pid) {
    if (!graphics || graphics_owner != pid) return;
    keyboard_set_mode(KBD_XLATE);
    console_set_graphics(false, 0);
}

// For terminal output: small changes (an echoed key, a cursor) go out at
// once; big ones (a scrolled screen) wait for the timer, which pushes them
// at most ~60 times a second -- a program printing thousands of lines then
// pays for 60 screens a second, not one per line.
static volatile bool flush_pending;
static uint32_t last_flush_tick;

void terminal_flush_lazy(void) {
    if (dirty_top == UINT32_MAX) return;
    if (dirty_bottom - dirty_top <= 2 * FONT_HEIGHT) {
        terminal_flush();
        flush_pending = false;
        return;
    }
    flush_pending = true;
}

// From the timer interrupt (250 Hz).
void console_tick(uint32_t now) {
    if (flush_pending && now - last_flush_tick >= 4) {
        flush_pending = false;
        last_flush_tick = now;
        terminal_flush();
    }
}

// ---- cell-level helpers for the VT emulator (drivers/vt.c) ----

// Copy a block of character cells, as memmove would: the source and
// destination may overlap.
void console_move_cells(uint32_t dst_col, uint32_t dst_row, uint32_t src_col, uint32_t src_row,
                        uint32_t cols, uint32_t rows) {
    if (!cols || !rows) return;
    uint32_t bpp = fb_bpp / 8;
    uint32_t bytes = cols * FONT_WIDTH * bpp;
    bool upward = dst_row < src_row || (dst_row == src_row && dst_col <= src_col);
    for (uint32_t i = 0; i < rows; i++) {
        uint32_t r = upward ? i : rows - 1 - i;
        for (uint32_t line = 0; line < FONT_HEIGHT; line++) {
            memmove(row_line(dst_row + r, line) + dst_col * FONT_WIDTH * bpp,
                    row_line(src_row + r, line) + src_col * FONT_WIDTH * bpp, bytes);
        }
    }
    mark_dirty(dst_row * FONT_HEIGHT);
    mark_dirty((dst_row + rows - 1) * FONT_HEIGHT);
}

void console_fill_cells(uint32_t col, uint32_t row, uint32_t cols, uint32_t rows, uint32_t bg) {
    if (!cols || !rows) return;
    if (fb_bpp != 32) {
        for (uint32_t r = row; r < row + rows; r++)
            for (uint32_t c = col; c < col + cols; c++) draw_char(c * FONT_WIDTH, r * FONT_HEIGHT, ' ', bg, bg);
        return;
    }
    uint32_t value = pack(bg);
    for (uint32_t r = row; r < row + rows; r++) {
        for (uint32_t line = 0; line < FONT_HEIGHT; line++) {
            fill32((uint32_t*)(row_line(r, line) + col * FONT_WIDTH * 4), value, (uint64_t)cols * FONT_WIDTH);
        }
    }
    mark_dirty(row * FONT_HEIGHT);
    mark_dirty((row + rows - 1) * FONT_HEIGHT);
}

// Scroll the whole screen n text rows up (n > 0) or down (n < 0), bringing
// in blank rows of colour bg: just a turn of the ring.
void console_scroll(int n, uint32_t bg) {
    uint32_t rows = characters_tall;
    if (!rows) return;
    while (n > 0) {
        ring_top = (ring_top + 1) % rows;
        console_fill_cells(0, rows - 1, characters_wide, 1, bg);
        n--;
    }
    while (n < 0) {
        ring_top = (ring_top + rows - 1) % rows;
        console_fill_cells(0, 0, characters_wide, 1, bg);
        n++;
    }
    dirty_top = 0;
    dirty_bottom = rows * FONT_HEIGHT;
}

// Flip a cell's colours: the VT's cursor.
void console_invert_cell(uint32_t col, uint32_t row) {
    uint32_t bpp = fb_bpp / 8;
    for (uint32_t y = 0; y < FONT_HEIGHT; y++) {
        uint8_t* p = row_line(row, y) + col * FONT_WIDTH * bpp;
        if (bpp == 4) {
            for (uint32_t i = 0; i < FONT_WIDTH; i++) ((uint32_t*)p)[i] ^= 0x00FFFFFF;   // alpha stays
            continue;
        }
        for (uint32_t b = 0; b < FONT_WIDTH * bpp; b++) p[b] = (uint8_t)~p[b];
    }
    mark_dirty(row * FONT_HEIGHT);
}
