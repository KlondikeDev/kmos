// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <types.h>
#include <vfs.h>
#include <console.h>
#include <klog.h>
#include <klibc/string.h>

typedef struct {
    char point[VFS_PATH_MAX];
    const vfs_ops_t* ops;
} mount_t;

static mount_t mounts[VFS_MAX_MOUNTS];
static u32 mount_count = 0;
static char cwd[VFS_PATH_MAX] = "/";

// Resolve `path` against the working directory into a normalised absolute
// path: no ".", no "..", no doubled or trailing slashes. ".." at the root
// stays at the root. Fails if the result would not fit.
static bool normalize(const char* path, char* out) {
    u32 len = 0;
    out[0] = '\0';

    const char* sources[2] = { path[0] == '/' ? "" : cwd, path };
    for (int s = 0; s < 2; s++) {
        const char* p = sources[s];
        while (*p) {
            while (*p == '/') p++;
            if (!*p) break;
            const char* start = p;
            while (*p && *p != '/') p++;
            u32 n = (u32)(p - start);

            if (n == 1 && start[0] == '.') continue;
            if (n == 2 && start[0] == '.' && start[1] == '.') {
                while (len > 0 && out[len - 1] != '/') len--;   // drop name
                if (len > 0) len--;                             // and its slash
                out[len] = '\0';
                continue;
            }
            if (len + 1 + n + 1 > VFS_PATH_MAX) return false;
            out[len++] = '/';
            for (u32 i = 0; i < n; i++) out[len++] = start[i];
            out[len] = '\0';
        }
    }
    if (len == 0) { out[0] = '/'; out[1] = '\0'; }
    return true;
}

// Does mount point `point` contain absolute path `path`?
static bool under(const char* point, const char* path) {
    if (strcmp(point, "/") == 0) return true;
    u32 n = (u32)strlen(point);
    return strncmp(point, path, n) == 0 && (path[n] == '\0' || path[n] == '/');
}

// Find the mount with the longest point containing `path`, and the path
// within that filesystem.
static const mount_t* resolve(const char* path, const char** rel) {
    const mount_t* best = NULL;
    u32 best_len = 0;
    for (u32 i = 0; i < mount_count; i++) {
        u32 n = (u32)strlen(mounts[i].point);
        if (under(mounts[i].point, path) && (!best || n > best_len)) {
            best = &mounts[i];
            best_len = n;
        }
    }
    if (!best) return NULL;
    if (strcmp(best->point, "/") == 0) *rel = path;
    else *rel = path[best_len] ? path + best_len : "/";
    return best;
}

bool vfs_mount(const char* point, const vfs_ops_t* ops) {
    if (mount_count == VFS_MAX_MOUNTS) return false;
    mount_t* m = &mounts[mount_count];
    if (!normalize(point, m->point)) return false;
    m->ops = ops;
    mount_count++;
    klog(LOG_OKAY, "vfs: mounted %s at %s\n", ops->type, m->point);
    return true;
}

i32 vfs_read_file(const char* path, void* buf, u32 max) {
    char abs[VFS_PATH_MAX];
    const char* rel;
    if (!normalize(path, abs)) return -1;
    const mount_t* m = resolve(abs, &rel);
    if (!m) return -1;
    return m->ops->read_file(rel, buf, max);
}

// Mount points directly inside `dir` show up in its listing even if the
// filesystem underneath has no such directory. They are listed first, so a
// given index means the same entry no matter how many the driver has.
static bool mount_child(const char* dir, u32 index, char* name_out) {
    u32 seen = 0;
    u32 dlen = (u32)strlen(dir);
    for (u32 i = 0; i < mount_count; i++) {
        const char* pt = mounts[i].point;
        if (strcmp(pt, "/") == 0 || !under(dir, pt) || strcmp(pt, dir) == 0) continue;
        const char* rest = pt + (dlen == 1 ? 1 : dlen + 1);
        bool direct = true;
        for (const char* c = rest; *c; c++) if (*c == '/') direct = false;
        if (!direct) continue;
        if (seen++ == index) {
            u32 k = 0;
            for (; rest[k] && k < VFS_NAME_MAX - 1; k++) name_out[k] = rest[k];
            name_out[k] = '\0';
            return true;
        }
    }
    return false;
}

static u32 mount_children(const char* dir) {
    char tmp[VFS_NAME_MAX];
    u32 n = 0;
    while (mount_child(dir, n, tmp)) n++;
    return n;
}

i32 vfs_readdir(const char* path, u32 index, char* name_out, u32* size_out, bool* is_dir_out) {
    char abs[VFS_PATH_MAX];
    const char* rel;
    if (!normalize(path, abs)) return -1;
    const mount_t* m = resolve(abs, &rel);
    if (!m) return -1;

    u32 synthetic = mount_children(abs);
    if (index < synthetic) {
        mount_child(abs, index, name_out);
        *size_out = 0;
        *is_dir_out = true;
        return 1;
    }
    i32 r = m->ops->readdir(rel, index - synthetic, name_out, size_out, is_dir_out);
    // A mount point is a directory even if the driver underneath disagrees.
    if (r < 0 && synthetic > 0) return 0;
    return r;
}

bool vfs_chdir(const char* path) {
    char abs[VFS_PATH_MAX];
    if (!normalize(path, abs)) return false;
    char name[VFS_NAME_MAX];
    u32 size;
    bool is_dir;
    // A directory is anything readdir does not reject outright.
    if (vfs_readdir(abs, 0, name, &size, &is_dir) < 0) return false;
    for (u32 i = 0; i < VFS_PATH_MAX; i++) {
        cwd[i] = abs[i];
        if (!abs[i]) break;
    }
    return true;
}

const char* vfs_pwd(void) {
    return cwd;
}

void vfs_list_mounts(void) {
    for (u32 i = 0; i < mount_count; i++) {
        terminal_printf(SILVER, BLACK, "%s on %s\n", mounts[i].ops->type, mounts[i].point);
    }
}
