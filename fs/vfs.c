// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <types.h>
#include <vfs.h>
#include <proc.h>
#include <file.h>
#include <pagecache.h>
#include <disk.h>
#include <errno.h>
#include <console.h>
#include <klog.h>
#include <klibc/string.h>
#include <memset.h>

static mount_t mounts[VFS_MAX_MOUNTS];
static u32 mount_count = 0;

// ---------------------------------------------------------------------------
// Permissions
// ---------------------------------------------------------------------------

void vfs_attrs(const vnode_t* n, u32* mode, u32* uid, u32* gid) {
    if (n->mode) {
        *mode = n->mode & 07777;
        *uid = n->uid;
        *gid = n->gid;
        return;
    }
    *uid = *gid = 0;
    const char* t = n->mnt && n->mnt->ops ? n->mnt->ops->type : "";
    if (!strcmp(t, "devfs")) *mode = n->type == VN_DIR ? 0755 : 0666;
    else if (!strcmp(t, "proc")) *mode = n->type == VN_DIR ? 0555 : n->type == VN_LNK ? 0777 : 0444;
    else *mode = 0755;                               // FAT32: root's, readable by all
}

static bool in_groups(const process_t* p, u32 gid, bool real) {
    if ((real ? p->gid : p->egid) == gid) return true;
    for (u32 i = 0; i < p->ngroups; i++) if (p->groups[i] == gid) return true;
    return false;
}

bool vfs_may_as(const vnode_t* n, u32 want, u32 uid, u32 gid, bool real) {
    (void)gid;
    u32 mode, owner, group;
    vfs_attrs(n, &mode, &owner, &group);
    if (uid == 0) {
        // Root: everything -- but only executes what someone could.
        return !(want & VFS_X) || n->type == VN_DIR || (mode & 0111);
    }
    process_t* p = proc_current();
    u32 bits = uid == owner ? mode >> 6 : in_groups(p, group, real) ? mode >> 3 : mode;
    return (bits & want & 7) == (want & 7);
}

bool vfs_may(const vnode_t* n, u32 want) {
    process_t* p = proc_current();
    return vfs_may_as(n, want, p->euid, p->egid, false);
}

// The sticky bit (as on /tmp): only a file's owner, the directory's owner,
// or root may unlink or rename it.
static bool sticky_allows(const vnode_t* dir, const vnode_t* node) {
    u32 dmode, duid, dgid, mode, uid, gid;
    vfs_attrs(dir, &dmode, &duid, &dgid);
    if (!(dmode & 01000)) return true;
    vfs_attrs(node, &mode, &uid, &gid);
    u32 me = proc_current()->euid;
    return me == 0 || me == uid || me == duid;
}

const mount_t* vfs_mount_at(u32 i) {
    return i < mount_count ? &mounts[i] : NULL;
}

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
    return vfs_mount_with(point, ops, NULL);
}

bool vfs_mount_with(const char* point, const vfs_ops_t* ops, void* priv) {
    if (mount_count == VFS_MAX_MOUNTS) return false;
    mount_t* m = &mounts[mount_count];
    // Mounting happens before any process has a cwd worth trusting, so the
    // point must already be absolute and clean.
    u32 i = 0;
    for (; point[i] && i < VFS_PATH_MAX - 1; i++) m->point[i] = point[i];
    m->point[i] = '\0';
    m->ops = ops;
    m->priv = priv;
    m->index = mount_count;
    mount_count++;
    klog(LOG_OKAY, "vfs: mounted %s at %s\n", ops->type, m->point);
    return true;
}

#define VFS_MAX_LINKS 32       // symbolic links followed in one lookup

// Walk an absolute, normalised path to a node. Symbolic links met on the way
// are followed by splicing their target into the path and starting again
// (a relative target is relative to the link's directory); a link as the
// final component only if follow_last is set.
//
// ".." was folded away textually before the walk, so "a/link/.." is "a" --
// the shell's view (as with zsh's default CHASE_LINKS off) rather than the
// target's parent.
static i32 walk(const char* abs_in, vnode_t* out, bool follow_last) {
    char path[VFS_PATH_MAX];
    strcpy(path, abs_in);
    u32 links = 0;

restart:;
    const char* rel;
    const mount_t* m = resolve_mount(path, &rel);
    if (!m) return -ENOENT;
    // Drivers fill in what they know; the rest (attributes FAT32 or devfs
    // do not keep) must read as zero, not as stack garbage.
    memset(out, 0, sizeof(*out));
    out->mnt = m;
    i32 r = m->ops->root(out);
    if (r < 0) return r;
    out->mnt = m;

    char name[VFS_NAME_MAX];
    const char* p = rel;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char* start = p;
        u32 n = 0;
        while (*p && *p != '/') name[n++] = *p++;
        name[n] = '\0';

        if (out->type != VN_DIR) return -ENOTDIR;
        if (!vfs_may(out, VFS_X)) return -EACCES;        // search permission
        vnode_t next = {0};
        memset(&next, 0, sizeof(next));
        r = m->ops->lookup(out, name, &next);
        if (r < 0) return r;
        next.mnt = m;

        const char* rest = p;
        while (*rest == '/') rest++;
        if (next.type == VN_LNK && (*rest || follow_last)) {
            if (!*rest && m->ops->follow && m->ops->follow(&next, out) == 0) return 0;
            if (++links > VFS_MAX_LINKS || !m->ops->readlink) return -ELOOP;
            char target[VFS_PATH_MAX];
            i64 len = m->ops->readlink(&next, target, sizeof(target) - 1);
            if (len < 0) return (i32)len;
            if (len == 0) return -ENOENT;
            target[len] = '\0';
            // new path = (target is absolute ? "" : the link's directory)
            //            + "/" + target + "/" + the rest
            char spliced[VFS_PATH_MAX * 2];
            u32 k = 0;
            if (target[0] != '/') {
                u32 dirlen = (u32)(start - path);
                for (u32 i = 0; i < dirlen; i++) spliced[k++] = path[i];
                spliced[k++] = '/';
            }
            for (i64 i = 0; i < len; i++) spliced[k++] = target[i];
            if (*rest) {
                spliced[k++] = '/';
                for (const char* c = rest; *c && k < sizeof(spliced) - 1; c++) spliced[k++] = *c;
            }
            spliced[k] = '\0';
            if (spliced[0] != '/') return -ENOENT;
            r = vfs_normalize(spliced, path);
            if (r < 0) return r;
            goto restart;
        }
        *out = next;
    }
    return 0;
}

i32 vfs_lookup(const char* path, vnode_t* out) {
    char abs[VFS_PATH_MAX];
    i32 r = vfs_normalize(path, abs);
    if (r < 0) return r;
    return walk(abs, out, true);
}

i32 vfs_lookup_nofollow(const char* path, vnode_t* out) {
    char abs[VFS_PATH_MAX];
    i32 r = vfs_normalize(path, abs);
    if (r < 0) return r;
    return walk(abs, out, false);
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
    r = walk(abs, dir_out, true);
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
    if (node->type == VN_REG && vnode_exec_busy(node)) return -ETXTBSY;
    pagecache_forget(node);
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
    memset(out, 0, sizeof(*out));
    i32 r = dir->mnt->ops->readdir(dir, index, name_out, out);
    if (r == 1) out->mnt = dir->mnt;
    return r;
}

// Directory listing by path, including mount points.
i32 vfs_readdir_path(const char* path, u32 index, char* name_out, vnode_t* out) {
    char abs[VFS_PATH_MAX];
    i32 r = vfs_normalize(path, abs);
    if (r < 0) return r;
    vnode_t dir = {0};
    r = walk(abs, &dir, true);
    if (r < 0) return r;
    if (dir.type != VN_DIR) return -ENOTDIR;

    u32 synthetic = 0;
    const mount_t* m;
    while (mount_child(abs, synthetic, name_out, &m)) {
        if (synthetic == index) {
            memset(out, 0, sizeof(*out));
            out->mnt = m;
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
    return vfs_create_mode(path, type, type == VN_DIR ? 0777 : 0666, out);
}

i32 vfs_create_mode(const char* path, u32 type, u32 mode, vnode_t* out) {
    mode &= 07777 & ~proc_current()->umask;
    vnode_t dir = {0};
    char name[VFS_NAME_MAX];
    i32 r = vfs_lookup_parent(path, &dir, name);
    if (r == -EBUSY) return -EEXIST;                     // a mount point: it exists, certainly
    if (r < 0) return r;
    vnode_t existing = {0};
    if (dir.mnt->ops->lookup(&dir, name, &existing) == 0) return -EEXIST;
    if (!vfs_may(&dir, VFS_W | VFS_X)) return -EACCES;
    if (!dir.mnt->ops->create) return -EROFS;
    memset(out, 0, sizeof(*out));
    r = dir.mnt->ops->create(&dir, name, type, mode, out);
    if (r == 0) out->mnt = dir.mnt;
    return r;
}

i32 vfs_unlink(const char* path, bool want_dir) {
    vnode_t dir = {0}, node = {0};
    char name[VFS_NAME_MAX];
    i32 r = vfs_lookup_parent(path, &dir, name);
    if (r < 0) return r;
    r = dir.mnt->ops->lookup(&dir, name, &node);
    if (r < 0) return r;
    if (want_dir && node.type != VN_DIR) return -ENOTDIR;
    if (!want_dir && node.type == VN_DIR) return -EISDIR;
    node.mnt = dir.mnt;
    if (!vfs_may(&dir, VFS_W | VFS_X)) return -EACCES;
    if (!sticky_allows(&dir, &node)) return -EPERM;
    // Unix would keep an open file's data alive until the last close; the
    // drivers cannot do that yet, and freeing it under an open descriptor
    // would let later writes land in someone else's blocks. So: refuse.
    if (!dir.mnt->ops->keeps_unlinked && vnode_is_open(&node)) return -EBUSY;
    if (!dir.mnt->ops->unlink) return -EROFS;
    return dir.mnt->ops->unlink(&dir, name);
}

i32 vfs_rename(const char* oldpath, const char* newpath) {
    vnode_t olddir = {0}, newdir = {0}, node = {0};
    char oldname[VFS_NAME_MAX], newname[VFS_NAME_MAX];
    i32 r = vfs_lookup_parent(oldpath, &olddir, oldname);
    if (r < 0) return r;
    r = vfs_lookup_parent(newpath, &newdir, newname);
    if (r < 0) return r;
    r = olddir.mnt->ops->lookup(&olddir, oldname, &node);
    if (r < 0) return r;
    node.mnt = olddir.mnt;
    if (olddir.mnt != newdir.mnt) return -EXDEV;      // across filesystems: copy instead
    if (!vfs_may(&olddir, VFS_W | VFS_X) || !vfs_may(&newdir, VFS_W | VFS_X)) return -EACCES;
    if (!sticky_allows(&olddir, &node)) return -EPERM;
    // A directory moving to another parent rewrites its "..": it must be ours to write.
    if (node.type == VN_DIR && olddir.id != newdir.id && !vfs_may(&node, VFS_W)) return -EACCES;
    // Drivers whose ids are stable (inode numbers) can move open files; for
    // the others the locator is about to change under the open file.
    if (!node.mnt->ops->keeps_unlinked && vnode_is_open(&node)) return -EBUSY;

    vnode_t existing = {0};
    if (newdir.mnt->ops->lookup(&newdir, newname, &existing) == 0) {
        existing.mnt = newdir.mnt;
        if (existing.type == VN_DIR) return node.type == VN_DIR ? -ENOTEMPTY : -EISDIR;
        if (node.type == VN_DIR) return -ENOTDIR;
        if (existing.mnt->ops == node.mnt->ops && existing.id == node.id && existing.aux == node.aux) return 0;
        if (!sticky_allows(&newdir, &existing)) return -EPERM;
        if (!existing.mnt->ops->keeps_unlinked && vnode_is_open(&existing)) return -EBUSY;
    }
    if (!olddir.mnt->ops->rename) return -EROFS;
    return olddir.mnt->ops->rename(&olddir, oldname, &newdir, newname);
}

i32 vfs_truncate(vnode_t* node, u64 size) {
    if(!node)return -EINVAL;
    if (node->type == VN_DIR) return -EISDIR;
    if (node->type != VN_REG) return 0;                  // devices ignore it
    if (!node->mnt->ops->truncate) return -EROFS;
    if (vnode_exec_busy(node)) return -ETXTBSY;
    pagecache_forget(node);
    return node->mnt->ops->truncate(node, size);
}

i32 vfs_chdir(const char* path) {
    char abs[VFS_PATH_MAX];
    i32 r = vfs_normalize(path, abs);
    if (r < 0) return r;
    vnode_t n = {0};
    r = walk(abs, &n, true);
    if (r < 0) return r;
    if (n.type != VN_DIR) return -ENOTDIR;
    if (!vfs_may(&n, VFS_X)) return -EACCES;
    char* cwd = proc_current()->cwd;
    for (u32 i = 0; i < VFS_PATH_MAX; i++) {
        cwd[i] = abs[i];
        if (!abs[i]) break;
    }
    return 0;
}

i64 vfs_read_file(const char* path, void* buf, u64 max) {
    vnode_t node = {0};
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

i32 vfs_symlink(const char* target, const char* linkpath) {
    vnode_t dir = {0}, existing = {0}, out = {0};
    char name[VFS_NAME_MAX];
    if (!target[0]) return -ENOENT;
    if (strlen(target) >= VFS_PATH_MAX) return -ENAMETOOLONG;
    i32 r = vfs_lookup_parent(linkpath, &dir, name);
    if (r < 0) return r;
    if (dir.mnt->ops->lookup(&dir, name, &existing) == 0) return -EEXIST;
    if (!vfs_may(&dir, VFS_W | VFS_X)) return -EACCES;
    if (!dir.mnt->ops->symlink) return -EPERM;           // FAT32 has none
    return dir.mnt->ops->symlink(&dir, name, target, &out);
}

i32 vfs_link(const char* oldpath, const char* newpath) {
    vnode_t node = {0}, dir = {0}, existing = {0};
    char name[VFS_NAME_MAX];
    i32 r = vfs_lookup_nofollow(oldpath, &node);
    if (r < 0) return r;
    if (node.type == VN_DIR) return -EPERM;              // no hard links to directories
    r = vfs_lookup_parent(newpath, &dir, name);
    if (r < 0) return r;
    if (dir.mnt != node.mnt) return -EXDEV;
    if (dir.mnt->ops->lookup(&dir, name, &existing) == 0) return -EEXIST;
    if (!vfs_may(&dir, VFS_W | VFS_X)) return -EACCES;
    if (!dir.mnt->ops->link) return -EPERM;
    return dir.mnt->ops->link(&dir, name, &node);
}

i64 vfs_readlink(const char* path, char* buf, u64 len) {
    vnode_t node = {0};
    i32 r = vfs_lookup_nofollow(path, &node);
    if (r < 0) return r;
    if (node.type != VN_LNK || !node.mnt->ops->readlink) return -EINVAL;
    return node.mnt->ops->readlink(&node, buf, len);
}

i32 vfs_setattr(vnode_t* node, u32 mask, const vattr_t* a_in) {
    if(!node)return -95; // EOPNOTSUPP: sockets have no filesystem attributes.
    vattr_t a = *a_in;
    process_t* p = proc_current();
    u32 mode, uid, gid;
    vfs_attrs(node, &mode, &uid, &gid);
    bool root = p->euid == 0, owner = p->euid == uid;
    if (mask & VATTR_MODE) {
        if (!root && !owner) return -EPERM;
        // Only a member of the file's group may make it set-group-id.
        if (!root && (a.mode & 02000) && !in_groups(p, (mask & VATTR_GID) ? a.gid : gid, false)) a.mode &= ~02000u;
    }
    if (mask & VATTR_UID) {
        if (a.uid == uid) mask &= ~(u32)VATTR_UID;
        else if (!root) return -EPERM;                   // giving files away: root only
    }
    if (mask & VATTR_GID) {
        if (a.gid == gid) mask &= ~(u32)VATTR_GID;
        else if (!root && !(owner && in_groups(p, a.gid, false))) return -EPERM;
    }
    if ((mask & (VATTR_UID | VATTR_GID)) && node->type == VN_REG && !(mask & VATTR_MODE) && (mode & 06000)) {
        // A new owner does not inherit set-id powers.
        mask |= VATTR_MODE;
        a.mode = mode & ~06000u;
    }
    if (mask & (VATTR_ATIME | VATTR_MTIME)) {
        if (mask & VATTR_NOW) {
            if (!root && !owner && !vfs_may(node, VFS_W)) return -EACCES;
        } else if (!root && !owner) {
            return -EPERM;
        }
    }
    // Filesystems without attributes (FAT32, devices) quietly keep none.
    if (!mask || !node->mnt || !node->mnt->ops->setattr) return 0;
    return node->mnt->ops->setattr(node, mask & ~(u32)VATTR_NOW, &a);
}

i32 vfs_statfs(const vnode_t* node, vfs_statfs_t* out) {
    for (u32 i = 0; i < sizeof(*out); i++) ((u8*)out)[i] = 0;
    out->bsize = 4096;
    out->namemax = VFS_NAME_MAX - 1;
    if (!node->mnt || !node->mnt->ops->statfs) return 0;
    return node->mnt->ops->statfs(node->mnt, out);
}

void vfs_getattr(vnode_t* node) {
    if (node->mnt && node->mnt->ops->getattr) node->mnt->ops->getattr(node);
}

void vfs_shutdown_all(void) {
    vfs_sync_all();
    for (u32 i = 0; i < mount_count; i++) {
        if (mounts[i].ops->shutdown) mounts[i].ops->shutdown(&mounts[i]);
    }
    for (u8 i = 0; i < DISK_WHOLE_MAX; i++) if (disk_present(i)) disk_flush(i);
}

i32 vfs_sync(const vnode_t* node) {
    if (!node || !node->mnt) return -EINVAL;
    if (node->mnt->ops->sync) {
        i32 r = node->mnt->ops->sync(node->mnt);
        if (r < 0) return r;
    }
    // Covers filesystems without private metadata buffers, including FAT.
    for (u8 i = 0; i < DISK_WHOLE_MAX; i++) if (disk_present(i) && !disk_flush(i)) return -EIO;
    return 0;
}

i32 vfs_sync_all(void) {
    i32 result = 0;
    for (u32 i = 0; i < mount_count; i++) {
        if (mounts[i].ops->sync && mounts[i].ops->sync(&mounts[i]) < 0) result = -EIO;
    }
    for (u8 i = 0; i < DISK_WHOLE_MAX; i++) if (disk_present(i) && !disk_flush(i)) result = -EIO;
    return result;
}
