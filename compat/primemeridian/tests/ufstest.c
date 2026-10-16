// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// ufstest -- exercise the UFS2 driver through ordinary file I/O.
//
//   ufstest write    build a tree under /ufs/t, checking as it goes
//   ufstest verify   (after a reboot) check that everything written is
//                    still there, byte for byte, then clean it all up
//   ufstest          both, in one boot
//
// Contents are generated from (file, offset), so a reader can check any
// byte without keeping a copy. The geometry it aims at (block and fragment
// boundaries, the direct/indirect limits) is taken from statvfs-free
// constants matching build.sh's image: 8 KiB blocks, 1 KiB fragments.
// After the run, test.py checks the image with FreeBSD's fsck_ffs.

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BS     8192ULL                 // fs_bsize
#define FS     1024ULL                 // fs_fsize
#define NDADDR 12
#define NINDIR (BS / 8)
#define T      "/ufs/t"

static FILE* serial;
static int passed, failed;

static void say(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
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

// The byte at `off` of file number `id`.
static uint8_t pattern(uint32_t id, uint64_t off) {
    uint64_t x = (off >> 3) * 0x9E3779B97F4A7C15ULL ^ ((uint64_t)id << 40) ^ 0xD1B54A32D192ED03ULL;
    x ^= x >> 29;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 32;
    return (uint8_t)(x >> ((off & 7) * 8));
}

static uint8_t buf[256 * 1024];

static int write_pattern(int fd, uint32_t id, uint64_t off, uint64_t len) {
    while (len) {
        uint64_t n = len < sizeof(buf) ? len : sizeof(buf);
        for (uint64_t i = 0; i < n; i++) buf[i] = pattern(id, off + i);
        if (pwrite(fd, buf, n, (off_t)off) != (ssize_t)n) return -1;
        off += n;
        len -= n;
    }
    return 0;
}

// Check [off, off+len) holds the pattern, or zeros where zero is set.
static int check_range(int fd, uint32_t id, uint64_t off, uint64_t len, int zero) {
    while (len) {
        uint64_t n = len < sizeof(buf) ? len : sizeof(buf);
        if (pread(fd, buf, n, (off_t)off) != (ssize_t)n) return -1;
        for (uint64_t i = 0; i < n; i++) {
            uint8_t want = zero ? 0 : pattern(id, off + i);
            if (buf[i] != want) {
                say("   mismatch in file %u at %llu: %02x, wanted %02x\n", id,
                    (unsigned long long)(off + i), buf[i], want);
                return -1;
            }
        }
        off += n;
        len -= n;
    }
    return 0;
}

static off_t size_of(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 ? st.st_size : -1;
}

// ---------------------------------------------------------------------------

static const uint64_t sizes[] = {
    0, 1, 511, 512, FS - 1, FS, FS + 1, 3 * FS + 17, BS - 1, BS, BS + 1,
    NDADDR * BS - 1, NDADDR * BS, NDADDR * BS + 1, (NDADDR + 3) * BS + 100, 1500000,
};
#define NSIZES (sizeof(sizes) / sizeof(sizes[0]))

static char path[256];
static const char* P(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(path, sizeof(path), fmt, ap);
    va_end(ap);
    return path;
}

// Sparse files whose few bytes live behind double and triple indirect blocks.
static const uint64_t far_double = (NDADDR + NINDIR + 7) * BS + 123;
static const uint64_t far_triple = (NDADDR + NINDIR + NINDIR * NINDIR + 5) * BS + 77;

static void phase_write(void) {
    mkdir(T, 0755);
    CHECK("mkdir /ufs/t", access(T, F_OK) == 0);

    int ok = 1;
    for (unsigned i = 0; i < NSIZES; i++) {
        int fd = open(P(T "/size-%llu.bin", (unsigned long long)sizes[i]), O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd < 0 || write_pattern(fd, 100 + i, 0, sizes[i]) < 0 ||
            check_range(fd, 100 + i, 0, sizes[i], 0) < 0) ok = 0;
        if (fd >= 0) close(fd);
        if (size_of(path) != (off_t)sizes[i]) ok = 0;
    }
    CHECK("files of every awkward size (fragments, blocks, indirect)", ok);

    // Growing by small appends: the tail fragment grows in place or moves.
    int fd = open(T "/appended.bin", O_RDWR | O_CREAT | O_TRUNC | O_APPEND, 0644);
    ok = fd >= 0;
    for (uint64_t off = 0; ok && off < 40000; off += 333) {
        for (int i = 0; i < 333; i++) buf[i] = pattern(200, off + i);
        ok = write(fd, buf, 333) == 333;
    }
    ok = ok && check_range(fd, 200, 0, 40000 - 40000 % 333 + 333, 0) == 0;
    if (fd >= 0) close(fd);
    CHECK("333-byte appends past the 12 direct blocks", ok);

    // Holes, and overwriting the middle.
    fd = open(T "/holes.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
    ok = fd >= 0 && write_pattern(fd, 201, 0, 1000) == 0 && write_pattern(fd, 201, 5 * BS + 10, 3000) == 0 &&
         write_pattern(fd, 201, 30 * BS, 5) == 0;
    ok = ok && check_range(fd, 201, 1000, 5 * BS + 10 - 1000, 1) == 0 &&
         check_range(fd, 201, 5 * BS + 10, 3000, 0) == 0 &&
         check_range(fd, 201, 5 * BS + 3010, 30 * BS - (5 * BS + 3010), 1) == 0 &&
         check_range(fd, 201, 30 * BS, 5, 0) == 0;
    if (fd >= 0) close(fd);
    CHECK("holes read back as zeros", ok);

    fd = open(T "/size-1500000.bin", O_RDWR);
    ok = fd >= 0 && pwrite(fd, "MIDDLE", 6, 700000) == 6;
    char mid[6];
    ok = ok && pread(fd, mid, 6, 700000) == 6 && memcmp(mid, "MIDDLE", 6) == 0 &&
         check_range(fd, 100 + NSIZES - 1, 699990, 10, 0) == 0 &&
         check_range(fd, 100 + NSIZES - 1, 700006, 10, 0) == 0;
    if (fd >= 0) { write_pattern(fd, 100 + NSIZES - 1, 700000, 6); close(fd); }
    CHECK("overwrite in the middle of a big file", ok);

    fd = open(T "/far-double.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
    ok = fd >= 0 && write_pattern(fd, 202, far_double, 9000) == 0 &&
         check_range(fd, 202, far_double, 9000, 0) == 0 && check_range(fd, 202, far_double - 4000, 4000, 1) == 0;
    if (fd >= 0) close(fd);
    CHECK("a sparse file reaching through double indirect blocks", ok && size_of(T "/far-double.bin") == (off_t)(far_double + 9000));

    fd = open(T "/far-triple.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
    ok = fd >= 0 && write_pattern(fd, 203, far_triple, 100) == 0 && check_range(fd, 203, far_triple, 100, 0) == 0;
    if (fd >= 0) close(fd);
    CHECK("...and through triple indirect blocks (8 GiB in)", ok);

    // Truncation, down and up.
    fd = open(T "/trunc.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
    ok = fd >= 0 && write_pattern(fd, 204, 0, 20 * BS + 500) == 0;
    ok = ok && ftruncate(fd, 14 * BS + 3) == 0 && check_range(fd, 204, 0, 14 * BS + 3, 0) == 0;
    ok = ok && ftruncate(fd, 5 * BS + 2 * FS + 7) == 0 && check_range(fd, 204, 0, 5 * BS + 2 * FS + 7, 0) == 0;
    ok = ok && ftruncate(fd, 9 * BS) == 0 && check_range(fd, 204, 0, 5 * BS + 2 * FS + 7, 0) == 0 &&
         check_range(fd, 204, 5 * BS + 2 * FS + 7, 9 * BS - (5 * BS + 2 * FS + 7), 1) == 0;
    ok = ok && ftruncate(fd, 700) == 0 && ftruncate(fd, 3000) == 0 &&
         check_range(fd, 204, 0, 700, 0) == 0 && check_range(fd, 204, 700, 2300, 1) == 0;
    if (fd >= 0) close(fd);
    CHECK("truncate down, up, and down again (old bytes never come back)", ok);
    fd = open(T "/trunc.bin", O_RDWR);
    ok = fd >= 0 && ftruncate(fd, 0) == 0 && write_pattern(fd, 205, 0, 2 * BS) == 0 &&
         check_range(fd, 205, 0, 2 * BS, 0) == 0;
    if (fd >= 0) close(fd);
    CHECK("truncate to zero and rewrite", ok && size_of(T "/trunc.bin") == (off_t)(2 * BS));

    // A directory big enough to need many chunks and blocks.
    mkdir(T "/many", 0755);
    ok = 1;
    for (int i = 0; i < 400 && ok; i++) {
        fd = open(P(T "/many/file-number-%04d-with-a-longish-name", i), O_WRONLY | O_CREAT | O_EXCL, 0644);
        ok = fd >= 0 && write(fd, &i, sizeof(i)) == sizeof(i);
        if (fd >= 0) close(fd);
    }
    CHECK("400 files in one directory", ok);
    for (int i = 0; i < 400 && ok; i += 2) ok = unlink(P(T "/many/file-number-%04d-with-a-longish-name", i)) == 0;
    for (int i = 0; i < 100 && ok; i++) {
        fd = open(P(T "/many/n%d", i), O_WRONLY | O_CREAT | O_EXCL, 0644);
        ok = fd >= 0;
        if (fd >= 0) close(fd);
    }
    DIR* d = opendir(T "/many");
    int count = 0;
    struct dirent* e;
    while (d && (e = readdir(d))) if (e->d_name[0] != '.') count++;
    if (d) closedir(d);
    CHECK("delete every other one, add 100 more: readdir agrees", ok && count == 300);

    // Directory trees and renames.
    ok = mkdir(T "/a", 0755) == 0 && mkdir(T "/a/b", 0755) == 0 && mkdir(T "/a/b/c", 0755) == 0 &&
         mkdir(T "/z", 0755) == 0;
    fd = open(T "/a/b/c/deep.txt", O_WRONLY | O_CREAT, 0644);
    ok = ok && fd >= 0 && write(fd, "deep", 4) == 4;
    if (fd >= 0) close(fd);
    CHECK("nested directories", ok);
    errno = 0;
    CHECK("rmdir of a non-empty directory is ENOTEMPTY", rmdir(T "/a/b") < 0 && errno == ENOTEMPTY);
    CHECK("move a directory to another parent", rename(T "/a/b", T "/z/moved") == 0 &&
          access(T "/z/moved/c/deep.txt", F_OK) == 0 && access(T "/a/b", F_OK) < 0);
    struct stat st;
    CHECK("its \"..\" follows it", stat(T "/z/moved/..", &st) == 0 && chdir(T "/z/moved/c") == 0 &&
          access("../../../a", F_OK) == 0 && chdir("/") == 0);
    errno = 0;
    CHECK("a directory cannot move into itself", rename(T "/z", T "/z/moved/c/z") < 0 && errno == EINVAL);
    fd = open(T "/old.txt", O_WRONLY | O_CREAT, 0644);
    if (fd >= 0) { write(fd, "old", 3); close(fd); }
    fd = open(T "/new.txt", O_WRONLY | O_CREAT, 0644);
    if (fd >= 0) { write(fd, "replacement", 11); close(fd); }
    CHECK("rename over an existing file", rename(T "/new.txt", T "/old.txt") == 0 &&
          size_of(T "/old.txt") == 11 && access(T "/new.txt", F_OK) < 0);
    CHECK("rename within a directory", rename(T "/old.txt", T "/renamed-with-a-name-that-is-rather-long.txt") == 0 &&
          size_of(T "/renamed-with-a-name-that-is-rather-long.txt") == 11);
}

static void phase_verify(void) {
    int ok = 1;
    for (unsigned i = 0; i < NSIZES; i++) {
        int fd = open(P(T "/size-%llu.bin", (unsigned long long)sizes[i]), O_RDONLY);
        if (fd < 0 || size_of(path) != (off_t)sizes[i] || check_range(fd, 100 + i, 0, sizes[i], 0) < 0) {
            say("   %s is wrong\n", path);
            ok = 0;
        }
        if (fd >= 0) close(fd);
    }
    CHECK("after reboot: every file is intact", ok);
    int fd = open(T "/far-triple.bin", O_RDONLY);
    CHECK("after reboot: the triple-indirect byte run", fd >= 0 && check_range(fd, 203, far_triple, 100, 0) == 0);
    if (fd >= 0) close(fd);
    fd = open(T "/holes.bin", O_RDONLY);
    CHECK("after reboot: holes", fd >= 0 && check_range(fd, 201, 5 * BS + 10, 3000, 0) == 0 &&
          check_range(fd, 201, 1000, 5 * BS + 10 - 1000, 1) == 0);
    if (fd >= 0) close(fd);
    char deep[8] = "";
    fd = open(T "/z/moved/c/deep.txt", O_RDONLY);
    CHECK("after reboot: the moved tree", fd >= 0 && read(fd, deep, 8) == 4 && memcmp(deep, "deep", 4) == 0);
    if (fd >= 0) close(fd);

    // Clean up everything, deepest first.
    for (int i = 1; i < 400; i += 2) unlink(P(T "/many/file-number-%04d-with-a-longish-name", i));
    for (int i = 0; i < 100; i++) unlink(P(T "/many/n%d", i));
    ok = rmdir(T "/many") == 0;
    ok = ok && unlink(T "/z/moved/c/deep.txt") == 0 && rmdir(T "/z/moved/c") == 0 &&
         rmdir(T "/z/moved") == 0 && rmdir(T "/z") == 0 && rmdir(T "/a") == 0;
    for (unsigned i = 0; i < NSIZES; i++) ok = ok && unlink(P(T "/size-%llu.bin", (unsigned long long)sizes[i])) == 0;
    const char* rest[] = { "appended.bin", "holes.bin", "far-double.bin", "far-triple.bin", "trunc.bin",
                           "renamed-with-a-name-that-is-rather-long.txt" };
    for (unsigned i = 0; i < sizeof(rest) / sizeof(rest[0]); i++) ok = ok && unlink(P(T "/%s", rest[i])) == 0;
    ok = ok && rmdir(T) == 0;
    CHECK("remove the whole tree", ok && access(T, F_OK) < 0);
}

int main(int argc, char** argv) {
    serial = fopen("/dev/serial", "w");
    const char* mode = argc > 1 ? argv[1] : "both";
    say("ufstest: starting (%s)\n", mode);
    if (strcmp(mode, "verify") != 0) phase_write();
    if (strcmp(mode, "write") != 0) phase_verify();
    say("ufstest: %d passed, %d failed\n", passed, failed);
    return failed;
}
