// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// A VT100/ANSI terminal emulator on the framebuffer console: enough of what
// Linux's console understands (TERM=linux) for full-screen programs and
// line editors -- cursor movement and addressing, erasing, inserting and
// deleting characters and lines, scroll regions, colours, a visible cursor,
// and the status reports a program may ask for.
//
// It draws through the console (kernel/console.c) and shares its cursor, so
// kernel messages printed with terminal_printf land in the same place.
//
// Unlike the kernel's own terminal_putchar, '\n' here is a pure line feed,
// as on a real terminal; the tty's ONLCR supplies the carriage return.

#include <types.h>
#include <vt.h>
#include <console.h>
#include <tty.h>
#include <itoa.h>

extern uint32_t color_fg, color_bg;

#define MAX_PARAMS 16

enum { ST_NORMAL, ST_ESC, ST_CSI, ST_OSC, ST_OSC_ESC, ST_SKIP_ONE };

static int state = ST_NORMAL;
static u32 params[MAX_PARAMS];
static u32 nparams;
static bool private_mode;              // CSI ? ...
static bool wrap_pending;              // wrote the last column; wrap on next char
static bool autowrap = true;
static bool cursor_visible = true;
static bool cursor_drawn;
static u32 drawn_x, drawn_y;
static u32 top, bottom;                // scroll region, inclusive rows
static u32 saved_x, saved_y;

// Attributes.
static const u32 palette[16] = {
    0x000000, 0xAA0000, 0x00AA00, 0xAA5500, 0x0000AA, 0xAA00AA, 0x00AAAA, 0xD0D0D0,
    0x555555, 0xFF5555, 0x55FF55, 0xFFFF55, 0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF,
};
#define DEFAULT_FG 7
#define DEFAULT_BG 0
static u32 fg_index = DEFAULT_FG, bg_index = DEFAULT_BG;
static u32 fg_rgb = 0xD0D0D0, bg_rgb = 0x000000;
static bool fg_is_rgb, bg_is_rgb;
static bool bold, reverse;

static u32 cols(void) { return characters_wide; }
static u32 rows(void) { return characters_tall; }

static void apply_colors(void) {
    u32 f = fg_is_rgb ? fg_rgb : palette[(bold && fg_index < 8) ? fg_index + 8 : fg_index];
    u32 b = bg_is_rgb ? bg_rgb : palette[bg_index];
    color_fg = reverse ? b : f;
    color_bg = reverse ? f : b;
}

void vt_hide_cursor(void) {
    if (cursor_drawn) {
        console_invert_cell(drawn_x, drawn_y);
        cursor_drawn = false;
    }
}

static void show_cursor(void) {
    if (!cursor_visible || cursor_drawn) return;
    drawn_x = cursor_x < cols() ? cursor_x : cols() - 1;
    drawn_y = cursor_y < rows() ? cursor_y : rows() - 1;
    console_invert_cell(drawn_x, drawn_y);
    cursor_drawn = true;
}

void vt_flush(void) {
    show_cursor();
    terminal_flush_lazy();
}

void vt_init(void) {
    top = 0;
    bottom = rows() - 1;
    apply_colors();
}

static void fix_region(void) {
    // The console's size is only known after terminal_init.
    if (bottom == 0 || bottom >= rows()) bottom = rows() - 1;
    if (top > bottom) top = 0;
}

// ---- scrolling ----

static void scroll_up(u32 n) {
    fix_region();
    u32 height = bottom - top + 1;
    if (n > height) n = height;
    if (top == 0 && bottom == rows() - 1) { console_scroll((int)n, color_bg); return; }   // the whole screen
    console_move_cells(0, top, 0, top + n, cols(), height - n);
    console_fill_cells(0, bottom - n + 1, cols(), n, color_bg);
}

static void scroll_down(u32 n) {
    fix_region();
    u32 height = bottom - top + 1;
    if (n > height) n = height;
    if (top == 0 && bottom == rows() - 1) { console_scroll(-(int)n, color_bg); return; }
    console_move_cells(0, top + n, 0, top, cols(), height - n);
    console_fill_cells(0, top, cols(), n, color_bg);
}

static void line_feed(void) {
    fix_region();
    if (cursor_y == bottom) scroll_up(1);
    else if (cursor_y < rows() - 1) cursor_y++;
}

static void reverse_index(void) {
    fix_region();
    if (cursor_y == top) scroll_down(1);
    else if (cursor_y > 0) cursor_y--;
}

// ---- erasing and editing ----

static void erase_in_line(u32 mode) {
    u32 x = cursor_x < cols() ? cursor_x : cols() - 1;
    switch (mode) {
        case 0: console_fill_cells(x, cursor_y, cols() - x, 1, color_bg); break;
        case 1: console_fill_cells(0, cursor_y, x + 1, 1, color_bg); break;
        case 2: console_fill_cells(0, cursor_y, cols(), 1, color_bg); break;
    }
}

static void erase_in_display(u32 mode) {
    switch (mode) {
        case 0:
            erase_in_line(0);
            if (cursor_y + 1 < rows()) console_fill_cells(0, cursor_y + 1, cols(), rows() - cursor_y - 1, color_bg);
            break;
        case 1:
            if (cursor_y) console_fill_cells(0, 0, cols(), cursor_y, color_bg);
            erase_in_line(1);
            break;
        case 2:
        case 3:
            console_fill_cells(0, 0, cols(), rows(), color_bg);
            break;
    }
}

static void insert_chars(u32 n) {
    u32 x = cursor_x;
    if (x >= cols()) return;
    if (n > cols() - x) n = cols() - x;
    console_move_cells(x + n, cursor_y, x, cursor_y, cols() - x - n, 1);
    console_fill_cells(x, cursor_y, n, 1, color_bg);
}

static void delete_chars(u32 n) {
    u32 x = cursor_x;
    if (x >= cols()) return;
    if (n > cols() - x) n = cols() - x;
    console_move_cells(x, cursor_y, x + n, cursor_y, cols() - x - n, 1);
    console_fill_cells(cols() - n, cursor_y, n, 1, color_bg);
}

static void insert_lines(u32 n) {
    fix_region();
    if (cursor_y < top || cursor_y > bottom) return;
    u32 saved = top;
    top = cursor_y;
    scroll_down(n);
    top = saved;
}

static void delete_lines(u32 n) {
    fix_region();
    if (cursor_y < top || cursor_y > bottom) return;
    u32 saved = top;
    top = cursor_y;
    scroll_up(n);
    top = saved;
}

// ---- cursor ----

static void move_to(i64 x, i64 y) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= cols()) x = cols() - 1;
    if (y >= rows()) y = rows() - 1;
    cursor_x = (u32)x;
    cursor_y = (u32)y;
    wrap_pending = false;
}

// ---- SGR: colours and attributes ----

static void sgr(void) {
    if (nparams == 0) params[nparams++] = 0;
    for (u32 i = 0; i < nparams; i++) {
        u32 p = params[i];
        if (p == 0) {
            fg_index = DEFAULT_FG; bg_index = DEFAULT_BG;
            fg_is_rgb = bg_is_rgb = bold = reverse = false;
        } else if (p == 1) bold = true;
        else if (p == 22) bold = false;
        else if (p == 7) reverse = true;
        else if (p == 27) reverse = false;
        else if (p >= 30 && p <= 37) { fg_index = p - 30; fg_is_rgb = false; }
        else if (p == 39) { fg_index = DEFAULT_FG; fg_is_rgb = false; }
        else if (p >= 40 && p <= 47) { bg_index = p - 40; bg_is_rgb = false; }
        else if (p == 49) { bg_index = DEFAULT_BG; bg_is_rgb = false; }
        else if (p >= 90 && p <= 97) { fg_index = p - 90 + 8; fg_is_rgb = false; }
        else if (p >= 100 && p <= 107) { bg_index = p - 100 + 8; bg_is_rgb = false; }
        else if ((p == 38 || p == 48) && i + 1 < nparams) {
            // 38;5;n (256-colour: we keep the first 16) or 38;2;r;g;b
            bool is_fg = p == 38;
            if (params[i + 1] == 5 && i + 2 < nparams) {
                u32 n = params[i + 2];
                u32 rgb;
                if (n < 16) rgb = palette[n];
                else if (n < 232) {
                    n -= 16;
                    static const u8 level[6] = { 0, 95, 135, 175, 215, 255 };
                    rgb = (u32)level[n / 36] << 16 | (u32)level[(n / 6) % 6] << 8 | level[n % 6];
                } else {
                    u32 g = 8 + (n - 232) * 10;
                    rgb = g << 16 | g << 8 | g;
                }
                if (is_fg) { fg_rgb = rgb; fg_is_rgb = true; } else { bg_rgb = rgb; bg_is_rgb = true; }
                i += 2;
            } else if (params[i + 1] == 2 && i + 4 < nparams) {
                u32 rgb = (params[i + 2] & 0xFF) << 16 | (params[i + 3] & 0xFF) << 8 | (params[i + 4] & 0xFF);
                if (is_fg) { fg_rgb = rgb; fg_is_rgb = true; } else { bg_rgb = rgb; bg_is_rgb = true; }
                i += 4;
            }
        }
        // 2 (dim), 4 (underline), 5 (blink), ... are accepted and ignored.
    }
    apply_colors();
}

// ---- control sequences ----

static u32 param(u32 i, u32 def) {
    return (i < nparams && params[i]) ? params[i] : def;
}

static void reply_cursor_position(void) {
    char buf[32], num[12];
    u32 n = 0;
    buf[n++] = 0x1B;
    buf[n++] = '[';
    utoa(cursor_y + 1, num, 10);
    for (char* c = num; *c; c++) buf[n++] = *c;
    buf[n++] = ';';
    utoa(cursor_x + 1, num, 10);
    for (char* c = num; *c; c++) buf[n++] = *c;
    buf[n++] = 'R';
    buf[n] = '\0';
    tty_input_str(&tty_console, buf);
}

static void csi_dispatch(char final) {
    if (private_mode) {
        if (final == 'h' || final == 'l') {
            bool on = final == 'h';
            for (u32 i = 0; i < nparams; i++) {
                if (params[i] == 25) cursor_visible = on;
                else if (params[i] == 7) autowrap = on;
                // ?1 (application cursor keys), ?1049 (alternate screen),
                // ?2004 (bracketed paste)... accepted, ignored.
            }
        }
        return;
    }

    switch (final) {
        case 'A': move_to(cursor_x, (i64)cursor_y - param(0, 1)); break;
        case 'B': case 'e': move_to(cursor_x, (i64)cursor_y + param(0, 1)); break;
        case 'C': case 'a': move_to((i64)cursor_x + param(0, 1), cursor_y); break;
        case 'D': move_to((i64)cursor_x - param(0, 1), cursor_y); break;
        case 'E': move_to(0, (i64)cursor_y + param(0, 1)); break;
        case 'F': move_to(0, (i64)cursor_y - param(0, 1)); break;
        case 'G': case '`': move_to((i64)param(0, 1) - 1, cursor_y); break;
        case 'd': move_to(cursor_x, (i64)param(0, 1) - 1); break;
        case 'H': case 'f': move_to((i64)param(1, 1) - 1, (i64)param(0, 1) - 1); break;
        case 'J': erase_in_display(nparams ? params[0] : 0); break;
        case 'K': erase_in_line(nparams ? params[0] : 0); break;
        case 'L': insert_lines(param(0, 1)); break;
        case 'M': delete_lines(param(0, 1)); break;
        case 'P': delete_chars(param(0, 1)); break;
        case '@': insert_chars(param(0, 1)); break;
        case 'X': {
            u32 n = param(0, 1);
            if (cursor_x < cols()) {
                if (n > cols() - cursor_x) n = cols() - cursor_x;
                console_fill_cells(cursor_x, cursor_y, n, 1, color_bg);
            }
            break;
        }
        case 'S': scroll_up(param(0, 1)); break;
        case 'T': scroll_down(param(0, 1)); break;
        case 'r': {
            u32 t = param(0, 1) - 1, b = param(1, rows()) - 1;
            if (b >= rows()) b = rows() - 1;
            if (t < b) { top = t; bottom = b; }
            move_to(0, 0);
            break;
        }
        case 'm': sgr(); break;
        case 's': saved_x = cursor_x; saved_y = cursor_y; break;
        case 'u': move_to(saved_x, saved_y); break;
        case 'n':
            if (param(0, 0) == 6) reply_cursor_position();
            else if (param(0, 0) == 5) tty_input_str(&tty_console, "\x1B[0n");
            break;
        case 'c': tty_input_str(&tty_console, "\x1B[?6c"); break;      // "I am a VT102"
        default: break;                                  // unknown: ignore
    }
}

static void put_printable(u8 c) {
    if (wrap_pending) {
        wrap_pending = false;
        cursor_x = 0;
        line_feed();
    }
    if (cursor_x >= cols()) cursor_x = cols() - 1;
    draw_char(cursor_x * 8, cursor_y * 16, c, color_fg, color_bg);
    if (cursor_x + 1 < cols()) cursor_x++;
    else if (autowrap) wrap_pending = true;
}

void vt_putc(char ch) {
    u8 c = (u8)ch;
    vt_hide_cursor();

    switch (state) {
        case ST_ESC:
            state = ST_NORMAL;
            switch (c) {
                case '[': state = ST_CSI; nparams = 0; params[0] = 0; private_mode = false; return;
                case ']': state = ST_OSC; return;
                case '(': case ')': case '*': case '+': case '#': state = ST_SKIP_ONE; return;
                case '7': saved_x = cursor_x; saved_y = cursor_y; return;
                case '8': move_to(saved_x, saved_y); return;
                case 'D': line_feed(); return;
                case 'E': cursor_x = 0; line_feed(); return;
                case 'M': reverse_index(); return;
                case 'c':                                    // full reset
                    fg_index = DEFAULT_FG; bg_index = DEFAULT_BG;
                    fg_is_rgb = bg_is_rgb = bold = reverse = false;
                    apply_colors();
                    top = 0; bottom = rows() - 1;
                    erase_in_display(2);
                    move_to(0, 0);
                    return;
                default: return;                             // =, >, H, ...: ignore
            }
        case ST_CSI:
            if (c >= '0' && c <= '9') {
                if (nparams == 0) nparams = 1;
                params[nparams - 1] = params[nparams - 1] * 10 + (c - '0');
                return;
            }
            if (c == ';') {
                if (nparams == 0) nparams = 1;
                if (nparams < MAX_PARAMS) params[nparams++] = 0;
                return;
            }
            if (c == '?' || c == '>' || c == '=') { private_mode = true; return; }
            if (c >= 0x40 && c <= 0x7E) {
                state = ST_NORMAL;
                csi_dispatch((char)c);
                return;
            }
            if (c < 0x20) break;                             // controls still act
            return;                                          // intermediates: ignore
        case ST_OSC:
            // Operating system commands (window titles, palettes): skip to
            // BEL or ST (ESC \).
            if (c == 0x07) state = ST_NORMAL;
            else if (c == 0x1B) state = ST_OSC_ESC;
            return;
        case ST_OSC_ESC:
            state = c == '\\' ? ST_NORMAL : ST_OSC;
            return;
        case ST_SKIP_ONE:
            state = ST_NORMAL;
            return;
        default:
            break;
    }

    switch (c) {
        case 0x1B: state = ST_ESC; return;
        case '\r': cursor_x = 0; wrap_pending = false; return;
        case '\n': case 0x0B: case 0x0C: line_feed(); wrap_pending = false; return;
        case '\b':
            if (wrap_pending) wrap_pending = false;
            else if (cursor_x > 0) cursor_x--;
            return;
        case '\t': {
            u32 next = (cursor_x / 8 + 1) * 8;
            cursor_x = next < cols() ? next : cols() - 1;
            return;
        }
        case 0x07: case 0x0E: case 0x0F: case 0x00: case 0x7F:
            return;                                          // bell, charset shifts, NUL, DEL
        default:
            if (c < 0x20) return;
            put_printable(c);
    }
}

void vt_write(const char* s, u64 n) {
    for (u64 i = 0; i < n; i++) vt_putc(s[i]);
    vt_flush();
}
