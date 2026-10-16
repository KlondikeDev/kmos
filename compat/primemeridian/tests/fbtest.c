// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// fbtest -- the framebuffer, the mouse and the raw keyboard, used the way
// the X server (Xfbdev) uses them: fbdev ioctls and mmap on /dev/fb0,
// KD_GRAPHICS and K_MEDIUMRAW on the console, PS/2 packets from
// /dev/input/mice. Input it cannot make itself it asks the test harness
// for: a "monitor: <command>" line on the serial port goes to QEMU.
//
// Run as root, from the console.

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static FILE* serial;
static int passed, failed;

static void say(const char* fmt, ...) {
    va_list ap;
    if (serial) {
        va_start(ap, fmt);
        vfprintf(serial, fmt, ap);
        va_end(ap);
        fflush(serial);
    }
}

#define CHECK(name, cond) do { \
    int ok_ = (cond); \
    if (ok_) { passed++; say("PASS %s\n", name); } \
    else { failed++; say("FAIL %s  (line %d, errno %d: %s)\n", name, __LINE__, errno, strerror(errno)); } \
} while (0)

// Ask the harness to type into QEMU's monitor.
static void monitor(const char* cmd) {
    say("monitor: %s\n", cmd);
}

// Read whatever arrives on fd within ms milliseconds (at most len bytes).
static int read_for(int fd, unsigned char* buf, int len, int ms) {
    int got = 0;
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        int left = ms - (int)((now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000);
        if (left <= 0 || got >= len) return got;
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, left) <= 0) continue;
        ssize_t n = read(fd, buf + got, (size_t)(len - got));
        if (n > 0) got += (int)n;
    }
}

int main(void) {
    serial = fopen("/dev/serial", "w");
    say("fbtest: starting\n");

    // ---- /dev/fb0
    int fb = open("/dev/fb0", O_RDWR);
    CHECK("open /dev/fb0", fb >= 0);
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    memset(&var, 0, sizeof(var));
    memset(&fix, 0, sizeof(fix));
    CHECK("FBIOGET_VSCREENINFO", ioctl(fb, FBIOGET_VSCREENINFO, &var) == 0 &&
          var.xres >= 640 && var.yres >= 480 && var.bits_per_pixel == 32 &&
          var.red.length == 8 && var.green.length == 8 && var.blue.length == 8);
    CHECK("FBIOGET_FSCREENINFO", ioctl(fb, FBIOGET_FSCREENINFO, &fix) == 0 &&
          fix.visual == FB_VISUAL_TRUECOLOR && fix.line_length >= var.xres * 4 &&
          fix.smem_len >= fix.line_length * var.yres);
    struct fb_var_screeninfo want = var;
    want.xres = 320;
    want.yres = 200;
    CHECK("asking for another mode gets the one there is",
          ioctl(fb, FBIOPUT_VSCREENINFO, &want) == 0 && want.xres == var.xres && want.yres == var.yres);
    say("fbtest: %ux%u, %u bits, line %u bytes, red at %u\n",
        var.xres, var.yres, var.bits_per_pixel, fix.line_length, var.red.offset);

    uint32_t* px = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
    CHECK("mmap /dev/fb0", px != MAP_FAILED);
    CHECK("a private mapping is refused", mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fb, 0) == MAP_FAILED);
    if (px == MAP_FAILED) goto out;
    uint32_t stride = fix.line_length / 4;

    // ---- the console stops drawing in graphics mode
    int tty = open("/dev/tty0", O_RDWR);
    CHECK("open /dev/tty0", tty >= 0);
    int mode = -1;
    CHECK("KDGETMODE: text", ioctl(tty, KDGETMODE, &mode) == 0 && mode == KD_TEXT);
    struct vt_stat vts;
    int vtno = -1;
    CHECK("VT_OPENQRY, VT_GETSTATE", ioctl(tty, VT_OPENQRY, &vtno) == 0 && vtno == 1 &&
          ioctl(tty, VT_GETSTATE, &vts) == 0 && vts.v_active == 1);
    CHECK("KDSETMODE KD_GRAPHICS", ioctl(tty, KDSETMODE, KD_GRAPHICS) == 0 &&
          ioctl(tty, KDGETMODE, &mode) == 0 && mode == KD_GRAPHICS);

    const uint32_t pattern = 0x00336699;
    for (uint32_t y = 0; y < var.yres; y++)
        for (uint32_t x = 0; x < var.xres; x++) px[y * stride + x] = pattern;
    // Through read() too: the mapping is the device's memory.
    uint32_t back = 0;
    CHECK("the mapping is the framebuffer (pread sees it)",
          pread(fb, &back, 4, (off_t)(fix.line_length * 10 + 40)) == 4 && back == pattern);
    uint32_t mark = 0x00FF00FF;
    CHECK("and pwrite reaches the mapping", pwrite(fb, &mark, 4, 0) == 4 && px[0] == mark);
    px[0] = pattern;

    // Text written now must not reach the screen.
    for (int i = 0; i < 40; i++) dprintf(tty, "this line must not be seen %d\n", i);
    usleep(200000);                    // past the console's batched flush
    int damaged = 0;
    for (uint32_t y = 0; y < var.yres; y++)
        for (uint32_t x = 0; x < var.xres; x++) damaged += px[y * stride + x] != pattern;
    CHECK("in graphics mode the console leaves the screen alone", damaged == 0);

    // A forked child shares the mapping (MAP_SHARED).
    pid_t pid = fork();
    if (pid == 0) {
        px[stride * 5 + 5] = 0x00ABCDEF;
        _exit(0);
    }
    waitpid(pid, NULL, 0);
    CHECK("fork shares the mapping", px[stride * 5 + 5] == 0x00ABCDEF);

    CHECK("KDSETMODE KD_TEXT", ioctl(tty, KDSETMODE, KD_TEXT) == 0);
    int restored = 0;
    for (uint32_t y = 0; y < var.yres; y++)
        for (uint32_t x = 0; x < var.xres; x++) restored += px[y * stride + x] != pattern;
    CHECK("back in text mode, the console redraws the screen", restored > (int)(var.xres * var.yres / 2));

    // A process that dies holding the screen gives it back.
    pid = fork();
    if (pid == 0) {
        ioctl(tty, KDSETMODE, KD_GRAPHICS);
        ioctl(tty, KDSKBMODE, K_MEDIUMRAW);
        _exit(0);
    }
    waitpid(pid, NULL, 0);
    int kb = -1;
    CHECK("the screen and keyboard come back when their owner exits",
          ioctl(tty, KDGETMODE, &mode) == 0 && mode == KD_TEXT &&
          ioctl(tty, KDGKBMODE, &kb) == 0 && kb == K_XLATE);

    // ---- the mouse
    int mice = open("/dev/input/mice", O_RDWR | O_NONBLOCK);
    CHECK("open /dev/input/mice", mice >= 0);
    unsigned char b[64];
    while (read(mice, b, sizeof(b)) > 0) {}           // anything stale
    errno = 0;
    CHECK("nothing to read: EAGAIN", read(mice, b, 3) < 0 && errno == EAGAIN);
    unsigned char id = 0xF2;
    CHECK("PS/2 identify: ack, then a plain mouse (0)",
          write(mice, &id, 1) == 1 && read_for(mice, b, 2, 500) == 2 && b[0] == 0xFA && b[1] == 0x00);

    monitor("mouse_move 10 -5");
    int n = read_for(mice, b, 30, 2000);
    int dx = 0, dy = 0, good = n > 0 && n % 3 == 0;
    for (int i = 0; good && i < n; i += 3) {
        good = (b[i] & 0x08) != 0;
        dx += (b[i] & 0x10) ? (int)b[i + 1] - 256 : b[i + 1];
        dy += (b[i] & 0x20) ? (int)b[i + 2] - 256 : b[i + 2];
    }
    say("fbtest: mouse moved %d,%d in %d bytes\n", dx, dy, n);
    CHECK("mouse motion arrives as PS/2 packets", good && dx == 10 && dy == 5);   // PS/2's y is up

    monitor("mouse_button 1");
    n = read_for(mice, b, 3, 2000);
    CHECK("the left button", n == 3 && (b[0] & 0x07) == 0x01);
    monitor("mouse_button 0");
    n = read_for(mice, b, 3, 2000);
    CHECK("and its release", n == 3 && (b[0] & 0x07) == 0x00);

    // ---- the keyboard, raw: Linux keycodes, bit 7 on release
    struct termios saved, raw;
    tcgetattr(tty, &saved);
    raw = saved;
    cfmakeraw(&raw);
    tcsetattr(tty, TCSANOW, &raw);
    tcflush(tty, TCIFLUSH);
    CHECK("KDSKBMODE K_MEDIUMRAW", ioctl(tty, KDSKBMODE, K_MEDIUMRAW) == 0 &&
          ioctl(tty, KDGKBMODE, &kb) == 0 && kb == K_MEDIUMRAW);
    monitor("sendkey a");
    n = read_for(tty, b, 2, 2000);
    CHECK("a key's press and release, as keycodes", n == 2 && b[0] == 30 && b[1] == (30 | 0x80));
    monitor("sendkey up");
    n = read_for(tty, b, 2, 2000);
    CHECK("an extended key (Up: 103)", n == 2 && b[0] == 103 && b[1] == (103 | 0x80));
    ioctl(tty, KDSKBMODE, K_XLATE);
    monitor("sendkey b");
    n = read_for(tty, b, 1, 2000);
    CHECK("back to characters", n == 1 && b[0] == 'b');
    tcsetattr(tty, TCSANOW, &saved);

out:
    say("fbtest: %d passed, %d failed\n", passed, failed);
    return failed;
}
