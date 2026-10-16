// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <kprintf.h>

typedef struct {
    char* buf;
    u64 cap;
    u64 len;
} out_t;

static void put(out_t* o, char c) {
    if (o->len + 1 < o->cap) o->buf[o->len] = c;
    o->len++;
}

static void put_field(out_t* o, const char* s, u64 n, int width, bool left, char pad) {
    u64 w = width > 0 ? (u64)width : 0;
    if (!left) for (u64 i = n; i < w; i++) put(o, pad);
    for (u64 i = 0; i < n; i++) put(o, s[i]);
    if (left) for (u64 i = n; i < w; i++) put(o, ' ');
}

int kvsnprintf(char* buf, u64 cap, const char* fmt, va_list ap) {
    out_t o = { buf, cap, 0 };
    for (; *fmt; fmt++) {
        if (*fmt != '%') { put(&o, *fmt); continue; }
        fmt++;
        bool left = false;
        char pad = ' ';
        for (;; fmt++) {
            if (*fmt == '-') left = true;
            else if (*fmt == '0') pad = '0';
            else break;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        int lng = 0;
        while (*fmt == 'l') { lng++; fmt++; }
        if (*fmt == 'z') { lng = 1; fmt++; }

        char tmp[24];
        u64 n = 0;
        switch (*fmt) {
            case 'd': case 'i': {
                i64 v = lng ? va_arg(ap, i64) : va_arg(ap, int);
                bool neg = v < 0;
                u64 u = neg ? (u64)(-(v + 1)) + 1 : (u64)v;
                do { tmp[n++] = (char)('0' + u % 10); u /= 10; } while (u);
                if (neg) tmp[n++] = '-';
                goto reverse;
            }
            case 'u': case 'x': case 'X': case 'p': {
                u64 u = *fmt == 'p' ? (u64)va_arg(ap, void*) : lng ? va_arg(ap, u64) : va_arg(ap, unsigned);
                u32 base = *fmt == 'u' ? 10 : 16;
                const char* digits = *fmt == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
                do { tmp[n++] = digits[u % base]; u /= base; } while (u);
                if (*fmt == 'p') { tmp[n++] = 'x'; tmp[n++] = '0'; }
            reverse:
                for (u64 i = 0; i < n / 2; i++) { char t = tmp[i]; tmp[i] = tmp[n - 1 - i]; tmp[n - 1 - i] = t; }
                put_field(&o, tmp, n, width, left, left ? ' ' : pad);
                break;
            }
            case 's': {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                u64 len = 0;
                while (s[len]) len++;
                put_field(&o, s, len, width, left, ' ');
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                put_field(&o, &c, 1, width, left, ' ');
                break;
            }
            case '%': put(&o, '%'); break;
            case '\0': fmt--; break;
            default: put(&o, '%'); put(&o, *fmt); break;
        }
    }
    if (cap) buf[o.len < cap ? o.len : cap - 1] = '\0';
    return (int)o.len;
}

int ksnprintf(char* buf, u64 cap, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}
