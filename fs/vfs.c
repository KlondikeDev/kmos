// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <types.h>
#include <vfs.h>
#include <proc.h>
#include <file.h>
#include <errno.h>
#include <console.h>
#include <klog.h>
#include <klibc/string.h>

static mount_t mounts[VFS_MAX_MOUNTS];
static u32 mount_count = 0;

// Resolve `path` against the working directory into a normalised absolute
// path: no ".", no "..", no doubled or trailing slashes. ".." at the root
// stays at the root.
i32 vfs_normalize(const char* path, char* out) {
    u32 len = 0;
    out[0] = '\0';
    if (!path[0]) return -ENOENT;

    const char* sources[2] = { path[0] == '/' ? "" : proc_current()->cwd, path };
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
            if (n >= VFS_NAME_MAX) return -ENAMETOOLONG;
            if (len + 1 + n + 1 > VFS_PATH_MAX) return -ENAMETOOLONG;
            out[len++] = '/';
            for (u32 i = 0; i < n; i++) out[len++] = start[i];
            out[len] = '\0';
        }
    }
    if (len == 0) { out[0] = '/'; out[1] = '\0'; }
    return 0;
}

// Does mount point `point` contain absolute path `path`?
static bool under(const char* point, const char* path) {
    if (strcmp(point, "/") == 0) return true;
    u32 n = (u32)strlen(point);
    return strncmp(point, path, n) == 0 && (path[n] == '\0' || path[n] == '/');
}

// The mount with the longest point containing `path`, and the remainder.
static const mount_t* resolve_mount(const char* path, const char** rel) {
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
    *rel = (strcmp(best->point, "/") == 0) ? path : path + best_len;
    return best;
}

bool vfs_mount(const char* point, const vfs_ops_t* ops) {
    if (mount_count == VFS_MAX_MOUNTS) return false;
    mount_t* m = &mounts[mount_count];
    // Mounting happens before any process has a cwd worth trusting, so the
    // point must already be absolute and clean.
    u32 i = 0;
    for (; point[i] && i < VFS_PATH_MAX - 1; i++) m->point[i] = point[i];
    m->point[i] = '\0';
    m->ops = ops;
    m->index = mount_count;
    mount_count++;
    klog(LOG_OKAY, "vfs: mounted %s at %s\n", ops->type, m->point);
    return true;
}

// Walk an absolute, normalised path to a node.
static i32 walk(const char* abs, vnode_t* out) {
    const char* rel;
    const mount_t* m = resolve_mount(abs, &rel);
    if (!m) return -ENOENT;

    i32 r = m->ops->root(out);
    if (r < 0) return r;
    out->mnt = m;

    char name[VFS_NAME_MAX];
    const char* p = rel;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        u32 n = 0;
        while (*p && *p != '/') name[n++] = *p++;
        name[n] = '\0';

        if (out->type != VN_DIR) return -ENOTDIR;
        vnode_t next;
        r = m->ops->lookup(out, name, &next);
        if (r < 0) return r;
        next.mnt = m;
        *out = next;
    }
    return 0;
}

i32 vfs_lookup(const char* path, vnode_t* out) {
    char abs[VFS_PATH_MAX];
    i32 r = vfs_normalize(path, abs);
    if (r < 0) return r;
    return walk(abs, out);
}

i32 vfs_lookup_parent(const char* path, vnode_t* dir_out, char* name_out) {
    char abs[VFS_PATH_MAX];
    i32 r = vfs_normalize(path, abs);
    if (r < 0) return r;
    if (strcmp(abs, "/") == 0) return -EEXIST;          // the root has no parent

    u32 len = (u32)strlen(abs);
    u32 slash = len;
    while (slash > 0 && abs[slash - 1] != '/') slash--;
    for (u32 i = slash; i <= len; i++) name_out[i - slash] = abs[i];

    // A mount point cannot be created or removed through its parent.
    for (u32 i = 0; i < mount_count; i++) {
        if (strcmp(mounts[i].point, abs) == 0) return -EBUSY;
    }

    if (slash == 1) { abs[1] = '\0'; }                   // parent is "/"
    else abs[slash - 1] = '\0';
    r = walk(abs, dir_out);
    if (r < 0) return r;
    return dir_out->type == VN_DIR ? 0 : -ENOTDIR;
}

i64 vfs_read(vnode_t* node, u64 off, void* buf, u64 len) {
    if (node->type == VN_DIR) return -EISDIR;
    if (!node->mnt->ops->read) return -ENOSYS;
    return node->mnt->ops->read(node, off, buf, len);
}

i64 vfs_write(vnode_t* node, u64 off, const void* buf, u64 len) {
    if (node->type == VN_DIR) return -EISDIR;
    if (!node->mnt->ops->write) return -EROFS;
    return node->mnt->ops->write(node, off, buf, len);
}

// Mount points directly inside directory `dir` show up in its listing even if
// the filesystem underneath has no such directory. They are listed first, so
// a given index means the same entry however many the driver has.
static bool mount_child(const char* dir, u32 index, char* name_out, const mount_t** m_out) {
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
            *m_out = &mounts[i];
            return true;
        }
    }
    return false;
}

// Entries of one directory node. Does not know about mount points; listings
// that should show them go through vfs_readdir_path.
i32 vfs_readdir(const vnode_t* dir, u32 index, char* name_out, vnode_t* out) {
    if (dir->type != VN_DIR) return -ENOTDIR;
    i32 r = dir->mnt->ops->readdir(dir, index, name_out, out);
    if (r == 1) out->mnt = dir->mnt;
    return r;
}

// Directory listing by path, including mount points.
i32 vfs_readdir_path(const char* path, u32 index, char* name_out, vnode_t* out) {
    char abs[VFS_PATH_MAX];
    i32 r = vfs_normalize(path, abs);
    if (r < 0) return r;
    vnode_t dir;
    r = walk(abs, &dir);
    if (r < 0) return r;
    if (dir.type != VN_DIR) return -ENOTDIR;

    u32 synthetic = 0;
    const mount_t* m;
    while (mount_child(abs, synthetic, name_out, &m)) {
        if (synthetic == index) {
            r = m->ops->root(out);
            if (r < 0) return r;
            out->mnt = m;
            return 1;
        }
        synthetic++;
    }
    return vfs_readdir(&dir, index - synthetic, name_out, out);
}

i32 vfs_create(const char* path, u32 type, vnode_t* out) {
    vnode_t dir;
    char name[VFS_NAME_MAX];
    i32 r = vfs_lookup_parent(path, &dir, name);
    if (r < 0) return r;
    vnode_t existing;
    if (dir.mnt->ops->lookup(&dir, name, &existing) == 0) return -EEXIST;
    if (!dir.mnt->ops->create) return -EROFS;
    r = dir.mnt->ops->create(&dir, name, type, out);
    if (r == 0) out->mnt = dir.mnt;
    return r;
}

i32 vfs_unlink(const char* path, bool want_dir) {
    vnode_t dir, node;
    char name[VFS_NAME_MAX];
    i32 r = vfs_lookup_parent(path, &dir, name);
    if (r < 0) return r;
    r = dir.mnt->ops->lookup(&dir, name, &node);
    if (r < 0) return r;
    if (want_dir && node.type != VN_DIR) return -ENOTDIR;
    if (!want_dir && node.type == VN_DIR) return -EISDIR;
    node.mnt = dir.mnt;
    // Unix would keep an open file's data alive until the last close; the
    // drivers cannot do that yet, and freeing it under an open descriptor
    // would let later writes land in someone else's blocks. So: refuse.
    if (vnode_is_open(&node)) return -EBUSY;
    if (!dir.mnt->ops->unlink) return -EROFS;
    return dir.mnt->ops->unlink(&dir, name);
}

i32 vfs_rename(const char* oldpath, const char* newpath) {
    vnode_t olddir, newdir, node;
    char oldname[VFS_NAME_MAX], newname[VFS_NAME_MAX];
    i32 r = vfs_lookup_parent(oldpath, &olddir, oldname);
    if (r < 0) return r;
    r = vfs_lookup_parent(newpath, &newdir, newname);
    if (r < 0) return r;
    r = olddir.mnt->ops->lookup(&olddir, oldname, &node);
    if (r < 0) return r;
    node.mnt = olddir.mnt;
    if (olddir.mnt != newdir.mnt) return -EXDEV;      // across filesystems: copy instead
    if (vnode_is_open(&node)) return -EBUSY;          // its locator is about to change

    vnode_t existing;
    if (newdir.mnt->ops->lookup(&newdir, newname, &existing) == 0) {
        existing.mnt = newdir.mnt;
        if (existing.type == VN_DIR) return node.type == VN_DIR ? -ENOTEMPTY : -EISDIR;
        if (node.type == VN_DIR) return -ENOTDIR;
        if (existing.mnt->ops == node.mnt->ops && existing.id == node.id && existing.aux == node.aux) return 0;
        if (vnode_is_open(&existing)) return -EBUSY;
    }
    if (!olddir.mnt->ops->rename) return -EROFS;
    return olddir.mnt->ops->rename(&olddir, oldname, &newdir, newname);
}

i32 vfs_truncate(vnode_t* node, u64 size) {
    if (node->type == VN_DIR) return -EISDIR;
    if (node->type != VN_REG) return 0;                  // devices ignore it
    if (!node->mnt->ops->truncate) return -EROFS;
    return node->mnt->ops->truncate(node, size);
}

i32 vfs_chdir(const char* path) {
    char abs[VFS_PATH_MAX];
    i32 r = vfs_normalize(path, abs);
    if (r < 0) return r;
    vnode_t n;
    r = walk(abs, &n);
    if (r < 0) return r;
    if (n.type != VN_DIR) return -ENOTDIR;
    char* cwd = proc_current()->cwd;
    for (u32 i = 0; i < VFS_PATH_MAX; i++) {
        cwd[i] = abs[i];
        if (!abs[i]) break;
    }
    return 0;
}

i64 vfs_read_file(const char* path, void* buf, u64 max) {
    vnode_t node;
    i32 r = vfs_lookup(path, &node);
    if (r < 0) return r;
    if (node.type == VN_DIR) return -EISDIR;
    u64 want = node.size < max ? node.size : max;
    return vfs_read(&node, 0, buf, want);
}

void vfs_list_mounts(void) {
    for (u32 i = 0; i < mount_count; i++) {
        terminal_printf(SILVER, BLACK, "%s on %s\n", mounts[i].ops->type, mounts[i].point);
    }
}
