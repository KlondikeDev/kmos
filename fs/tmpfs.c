// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// tmpfs: a filesystem in kernel memory, mounted at /tmp. Long names, any
// characters but '/', and gone at reboot -- what scratch files want (and
// what FAT32's 8.3 names cannot give them yet).
//
// Every file and directory is a tnode. A vnode's id is its tnode's address,
// which never changes while the node exists, so open files and renames are
// simple. Contents are one kmalloc'd buffer per file, grown by doubling.

#include <types.h>
#include <tmpfs.h>
#include <vfs.h>
#include <memory.h>
#include <memcpy.h>
#include <memset.h>
#include <errno.h>
#include <rtc.h>
#include <file.h>
#include <klibc/string.h>

#define TMPFS_MAX_BYTES (32 * MB)       // all file contents together

typedef struct tnode {
    char name[VFS_NAME_MAX];
    u32 type;                           // VN_REG, VN_DIR or VN_LNK (target in data)
    u32 mode, uid, gid;
    bool orphan;                        // unlinked while open: freed on release
    u8* data;
    u64 size, cap;
    u64 mtime;
    struct tnode* parent;
    struct tnode* children;             // directories: first child
    struct tnode* next;                 // next sibling
} tnode_t;

static tnode_t root = { .name = "", .type = VN_DIR, .mode = 01777 };   // like /tmp everywhere
static u64 used_bytes;

static tnode_t* T(const vnode_t* n) {
    return (tnode_t*)n->id;
}

static void fill(tnode_t* t, vnode_t* out) {
    out->id = (u64)t;
    out->aux = 0;
    out->size = t->size;
    out->mtime = t->mtime;
    out->atime = out->ctime = t->mtime;
    out->type = t->type;
    out->mode = t->mode;
    out->uid = t->uid;
    out->gid = t->gid;
    out->nlink = t->type == VN_DIR ? 2 : 1;
}

static tnode_t* find(tnode_t* dir, const char* name) {
    for (tnode_t* c = dir->children; c; c = c->next) {
        if (!strcmp(c->name, name)) return c;
    }
    return NULL;
}

static void detach(tnode_t* t) {
    for (tnode_t** pp = &t->parent->children; *pp; pp = &(*pp)->next) {
        if (*pp == t) { *pp = t->next; break; }
    }
    t->next = NULL;
}

static void attach(tnode_t* dir, tnode_t* t) {
    t->parent = dir;
    t->next = dir->children;
    dir->children = t;
}

static i32 tmpfs_root(vnode_t* out) {
    if (!root.mtime) root.mtime = rtc_now();
    fill(&root, out);
    return 0;
}

static i32 tmpfs_lookup(const vnode_t* dir, const char* name, vnode_t* out) {
    if (T(dir)->type != VN_DIR) return -ENOTDIR;
    tnode_t* t = find(T(dir), name);
    if (!t) return -ENOENT;
    fill(t, out);
    return 0;
}

static i64 tmpfs_read(vnode_t* node, u64 off, void* buf, u64 len) {
    tnode_t* t = T(node);
    if (t->type == VN_DIR) return -EISDIR;
    if (off >= t->size) return 0;
    if (len > t->size - off) len = t->size - off;
    memcpy(buf, t->data + off, len);
    return (i64)len;
}

// Make room for `size` bytes, zero-filling anything new.
static i32 reserve(tnode_t* t, u64 size) {
    if (size <= t->cap) {
        if (size > t->size) memset(t->data + t->size, 0, (u32)(size - t->size));
        return 0;
    }
    u64 cap = t->cap ? t->cap : 256;
    while (cap < size) cap *= 2;
    if (used_bytes - t->cap + cap > TMPFS_MAX_BYTES) return -ENOSPC;
    u8* d = kmalloc(cap);
    if (!d) return -ENOSPC;
    if (t->size) memcpy(d, t->data, t->size);
    memset(d + t->size, 0, (u32)(cap - t->size));
    if (t->data) kfree(t->data);
    used_bytes += cap - t->cap;
    t->data = d;
    t->cap = cap;
    return 0;
}

static i64 tmpfs_write(vnode_t* node, u64 off, const void* buf, u64 len) {
    tnode_t* t = T(node);
    if (t->type == VN_DIR) return -EISDIR;
    if (len == 0) return 0;
    if (off + len < off) return -EFBIG;
    i32 r = reserve(t, off + len);
    if (r < 0) return r;
    memcpy(t->data + off, buf, len);
    if (off + len > t->size) t->size = off + len;
    t->mtime = rtc_now();
    node->size = t->size;               // the shared vnode every open sees
    return (i64)len;
}

static i32 tmpfs_truncate(vnode_t* node, u64 size) {
    tnode_t* t = T(node);
    if (size > t->size) {
        i32 r = reserve(t, size);
        if (r < 0) return r;
    }
    t->size = size;
    t->mtime = rtc_now();
    node->size = size;
    return 0;
}

static i32 tmpfs_readdir(const vnode_t* dir, u32 index, char* name_out, vnode_t* out) {
    if (T(dir)->type != VN_DIR) return -ENOTDIR;
    tnode_t* c = T(dir)->children;
    while (c && index--) c = c->next;
    if (!c) return 0;
    strcpy(name_out, c->name);
    fill(c, out);
    return 1;
}

static i32 tmpfs_create(const vnode_t* dir, const char* name, u32 type, u32 mode, vnode_t* out) {
    if (T(dir)->type != VN_DIR) return -ENOTDIR;
    if (strlen(name) >= VFS_NAME_MAX) return -ENAMETOOLONG;
    tnode_t* t = kmalloc(sizeof(tnode_t));
    if (!t) return -ENOSPC;
    memset(t, 0, sizeof(*t));
    strcpy(t->name, name);
    t->type = (type == VN_DIR || type == VN_SOCK) ? type : VN_REG;
    t->mode = mode & 07777;
    t->mtime = rtc_now();
    attach(T(dir), t);
    fill(t, out);
    return 0;
}

static void destroy(tnode_t* t) {
    if (t->data) {
        used_bytes -= t->cap;
        kfree(t->data);
    }
    kfree(t);
}

// Gone from the tree: free it now, or at its last close if it is open.
static void discard(const struct mount* m, tnode_t* t) {
    detach(t);
    vnode_t probe = { .mnt = m, .id = (u64)t };
    if (vnode_is_open(&probe)) t->orphan = true;
    else destroy(t);
}

static i32 tmpfs_unlink(const vnode_t* dir, const char* name) {
    tnode_t* t = find(T(dir), name);
    if (!t) return -ENOENT;
    if (t->type == VN_DIR && t->children) return -ENOTEMPTY;
    discard(dir->mnt, t);
    return 0;
}

static void tmpfs_release(vnode_t* node) {
    tnode_t* t = T(node);
    if (t->orphan) destroy(t);
}

static i32 tmpfs_symlink(const vnode_t* dir, const char* name, const char* target, vnode_t* out) {
    i32 r = tmpfs_create(dir, name, VN_REG, 0777, out);
    if (r < 0) return r;
    tnode_t* t = T(out);
    t->type = VN_LNK;
    u64 len = strlen(target);
    if ((r = reserve(t, len)) < 0) { detach(t); destroy(t); return r; }
    memcpy(t->data, target, len);
    t->size = len;
    fill(t, out);
    return 0;
}

static i64 tmpfs_readlink(vnode_t* node, char* buf, u64 len) {
    tnode_t* t = T(node);
    if (t->type != VN_LNK) return -EINVAL;
    if (len > t->size) len = t->size;
    memcpy(buf, t->data, len);
    return (i64)len;
}

static i32 tmpfs_setattr(vnode_t* node, u32 mask, const vattr_t* a) {
    tnode_t* t = T(node);
    if (mask & VATTR_MODE) t->mode = a->mode & 07777;
    if (mask & VATTR_UID) t->uid = a->uid;
    if (mask & VATTR_GID) t->gid = a->gid;
    if (mask & VATTR_MTIME) t->mtime = a->mtime;
    fill(t, node);
    return 0;
}

static i32 tmpfs_statfs(const struct mount* m, vfs_statfs_t* out) {
    (void)m;
    out->bsize = 4096;
    out->blocks = TMPFS_MAX_BYTES / 4096;
    out->bfree = out->bavail = (TMPFS_MAX_BYTES - used_bytes) / 4096;
    out->namemax = VFS_NAME_MAX - 1;
    return 0;
}

static i32 tmpfs_rename(const vnode_t* olddir, const char* oldname,
                        const vnode_t* newdir, const char* newname) {
    tnode_t* t = find(T(olddir), oldname);
    if (!t) return -ENOENT;
    if (strlen(newname) >= VFS_NAME_MAX) return -ENAMETOOLONG;
    // A directory cannot move inside itself.
    for (tnode_t* a = T(newdir); a; a = a->parent) {
        if (a == t) return -EINVAL;
    }
    tnode_t* existing = find(T(newdir), newname);
    if (existing == t) return 0;
    if (existing) {
        if (existing->type == VN_DIR && existing->children) return -ENOTEMPTY;
        discard(newdir->mnt, existing);
    }
    detach(t);
    strcpy(t->name, newname);
    attach(T(newdir), t);
    return 0;
}

const vfs_ops_t tmpfs_vfs_ops = {
    .type = "tmpfs",
    .root = tmpfs_root,
    .lookup = tmpfs_lookup,
    .read = tmpfs_read,
    .write = tmpfs_write,
    .readdir = tmpfs_readdir,
    .create = tmpfs_create,
    .unlink = tmpfs_unlink,
    .truncate = tmpfs_truncate,
    .rename = tmpfs_rename,
    .readlink = tmpfs_readlink,
    .symlink = tmpfs_symlink,
    .setattr = tmpfs_setattr,
    .statfs = tmpfs_statfs,
    .release = tmpfs_release,
    .keeps_unlinked = true,
};
