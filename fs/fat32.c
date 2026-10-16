// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// This file has been generated in whole or in part by a Large Language Model

#include <types.h>
#include <fat32.h>
#include <ata.h>
#include <console.h>
#include <klibc/string.h>
#include <vfs.h>
#include <errno.h>
#include <memset.h>
#include <rtc.h>

_Static_assert(sizeof(bpb_t) == 512, "bpb_t must be exactly one sector");
_Static_assert(sizeof(dirent_t) == 32, "dirent_t must be exactly 32 bytes");

// Filesystem geometry, cached at mount by fat32_init().
static uint32_t fat_start;            // first FAT, in sectors
static uint32_t data_start;           // first data sector
static uint32_t sectors_per_cluster;
static uint32_t root_cluster;
static uint32_t fat_count;            // copies of the FAT
static uint32_t fat_sectors;          // sectors per FAT copy
static uint32_t total_clusters;       // data clusters, numbered from 2
static uint32_t fsinfo_sector;        // 0 if the volume has none

// Navigation state.
static uint32_t current_dir;          // cluster of the working directory
static char     current_path[256] = "/";

#define DIRENTS_PER_SECTOR (512 / 32)

// ---- Phase 1: mount ----
bool fat32_init(void) {
    uint8_t sector[512];
    if (!read_sectors(0, sector)) return false;
    bpb_t* bpb = (bpb_t*)sector;

    sectors_per_cluster = bpb->sectors_per_cluster;
    root_cluster        = bpb->root_cluster;
    fat_start           = bpb->reserved_sectors;
    data_start          = bpb->reserved_sectors + (bpb->num_fats * bpb->fat_size_32);
    fat_count           = bpb->num_fats;
    fat_sectors         = bpb->fat_size_32;
    uint32_t total      = bpb->total_sectors_32 ? bpb->total_sectors_32 : bpb->total_sectors_16;
    total_clusters      = sectors_per_cluster ? (total - data_start) / sectors_per_cluster : 0;
    fsinfo_sector       = (bpb->fs_info && bpb->fs_info != 0xFFFF) ? bpb->fs_info : 0;
    // The FAT also has to have an entry for every cluster.
    if (total_clusters > fat_sectors * 128 - 2) total_clusters = fat_sectors * 128 - 2;

    current_dir = root_cluster;
    current_path[0] = '/';
    current_path[1] = '\0';
    return true;
}

// ---- Phase 2: cluster math + FAT walk ----
uint32_t cluster_to_lba(uint32_t cluster) {
    return data_start + (cluster - 2) * sectors_per_cluster;
}

uint32_t fat_next_cluster(uint32_t cluster) {
    uint32_t offset     = cluster * 4;            // FAT32 entries are 4 bytes
    uint32_t fat_sector = fat_start + offset / 512;
    uint32_t in_sector  = offset % 512;

    uint8_t buf[512];
    if (!read_sectors(fat_sector, buf)) return FAT_CLUSTER_EOC;

    uint32_t entry = *(uint32_t*)(buf + in_sector);
    return entry & FAT_ENTRY_MASK;                // low 28 bits are the cluster
}

// ---- 8.3 name helpers ----
// Decode an 11-byte 8.3 field ("README  TXT") into "README.TXT".
static void fat_format_name(const uint8_t* raw, char* out) {
    int o = 0;
    for (int i = 0; i < 8 && raw[i] != ' '; i++) out[o++] = raw[i];
    if (raw[8] != ' ') {
        out[o++] = '.';
        for (int i = 8; i < 11 && raw[i] != ' '; i++) out[o++] = raw[i];
    }
    out[o] = '\0';
}

static bool strchr_k(const char* s, char c) {
    for (; *s; s++) if (*s == c) return true;
    return false;
}

static char fat_upper(char c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

// Case-insensitive compare (8.3 names are stored uppercase on disk).
static bool fat_name_eq(const char* a, const char* b) {
    while (*a && *b) {
        if (fat_upper(*a) != fat_upper(*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

// Real entry? Filters deleted slots, long-name fragments, and volume labels.
static bool fat_entry_usable(const dirent_t* e) {
    if (e->name[0] == FAT_DIRENT_DELETED) return false;
    if (e->attr == FAT_ATTR_LONG_NAME) return false;
    if (e->attr & FAT_ATTR_VOLUME_ID) return false;
    return true;
}

// ---- directory scan ----
// Find `name` in the directory starting at dir_cluster. On a match, reports
// the entry's start cluster, size, and attribute byte. Directories are
// cluster chains like files, so we follow the chain to the end.
static bool dir_find(uint32_t dir_cluster, const char* name,
                     uint32_t* out_cluster, uint32_t* out_size, uint8_t* out_attr) {
    uint8_t buf[512];
    char fname[13];
    uint32_t cluster = dir_cluster;

    while (cluster < FAT_CLUSTER_EOC) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < sectors_per_cluster; s++) {
            if (!read_sectors(lba + s, buf)) return false;
            dirent_t* e = (dirent_t*)buf;
            for (int i = 0; i < DIRENTS_PER_SECTOR; i++) {
                if (e[i].name[0] == FAT_DIRENT_END) return false;  // end of dir
                if (!fat_entry_usable(&e[i])) continue;
                fat_format_name(e[i].name, fname);
                if (fat_name_eq(fname, name)) {
                    *out_cluster = ((uint32_t)e[i].cluster_hi << 16) | e[i].cluster_lo;
                    *out_size    = e[i].size;
                    *out_attr    = e[i].attr;
                    return true;
                }
            }
        }
        cluster = fat_next_cluster(cluster);
    }
    return false;
}

// ---- path resolution ----
// Resolve a path (absolute if it starts with '/', else relative to the
// working directory) to the cluster of the directory it names. Handles
// "." (stay), ".." (parent), and the FAT quirk that ".." to the root is
// stored as cluster 0. Fails if any component is missing or not a directory.
static bool resolve_path_dir(const char* path, uint32_t* out_cluster) {
    uint32_t cur = (*path == '/') ? root_cluster : current_dir;
    const char* p = path;
    if (*p == '/') p++;

    char comp[13];
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        int n = 0;
        while (*p && *p != '/' && n < 12) comp[n++] = *p++;
        comp[n] = '\0';
        while (*p && *p != '/') p++;          // discard any over-long remainder

        if (strcmp(comp, ".") == 0) continue;

        uint32_t c, sz;
        uint8_t attr;
        if (!dir_find(cur, comp, &c, &sz, &attr)) return false;
        if (!(attr & FAT_ATTR_DIRECTORY)) return false;
        if (c == 0) c = root_cluster;         // ".." to root is stored as 0
        cur = c;
    }
    *out_cluster = cur;
    return true;
}

// ---- working-directory path string (for pwd) ----
static void path_reset_root(void) {
    current_path[0] = '/';
    current_path[1] = '\0';
}

static void path_push(const char* comp) {
    int len = (int)strlen(current_path);
    if (!(len == 1 && current_path[0] == '/')) {  // avoid "//"
        if (len < 254) current_path[len++] = '/';
    }
    for (int i = 0; comp[i] && len < 255; i++) current_path[len++] = comp[i];
    current_path[len] = '\0';
}

static void path_pop(void) {
    int len = (int)strlen(current_path);
    if (len <= 1) { path_reset_root(); return; }
    len--;                                        // last char
    while (len > 0 && current_path[len] != '/') len--;
    if (len == 0) { path_reset_root(); return; }
    current_path[len] = '\0';
}

// ---- Phase 3: list a directory ----
static void fat32_list_dir(uint32_t dir_cluster) {
    uint8_t buf[512];
    char name[13];
    uint32_t cluster = dir_cluster;

    while (cluster < FAT_CLUSTER_EOC) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < sectors_per_cluster; s++) {
            if (!read_sectors(lba + s, buf)) return;
            dirent_t* e = (dirent_t*)buf;
            for (int i = 0; i < DIRENTS_PER_SECTOR; i++) {
                if (e[i].name[0] == FAT_DIRENT_END) return;
                if (!fat_entry_usable(&e[i])) continue;
                fat_format_name(e[i].name, name);
                if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
                if (e[i].attr & FAT_ATTR_DIRECTORY)
                    terminal_printf(CYAN, BLACK, "%s/\n", name);
                else
                    terminal_printf(SILVER, BLACK, "%s\t%u bytes\n", name, e[i].size);
            }
        }
        cluster = fat_next_cluster(cluster);
    }
}

// List the working directory.
void fat32_list(void) {
    fat32_list_dir(current_dir);
}

// Change the working directory. Updates both the cached cluster and the
// path string. resolve_path_dir validates the path first, so the path
// string update below only ever mirrors a known-good move.
bool fat32_chdir(const char* path) {
    uint32_t target;
    if (!resolve_path_dir(path, &target)) return false;
    current_dir = target;

    if (*path == '/') path_reset_root();
    const char* p = path;
    if (*p == '/') p++;

    char comp[13];
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        int n = 0;
        while (*p && *p != '/' && n < 12) comp[n++] = *p++;
        comp[n] = '\0';
        while (*p && *p != '/') p++;

        if (strcmp(comp, ".") == 0) continue;
        if (strcmp(comp, "..") == 0) { path_pop(); continue; }
        path_push(comp);
    }
    return true;
}

const char* fat32_pwd(void) {
    return current_path;
}

// ---- Phase 4: read a file by path into buffer ----
// Accepts "file", "dir/file", or "/abs/dir/file". Returns bytes read, or -1.
i32 fat32_read_file(const char* path, void* buffer, uint32_t max_bytes) {
    // Split the path into a parent directory and a final filename.
    const char* slash = NULL;
    for (const char* p = path; *p; p++) if (*p == '/') slash = p;

    uint32_t dir;
    const char* fname;
    if (slash) {
        if (slash == path) {
            dir = root_cluster;                   // "/file"
        } else {
            char dirpath[128];
            int n = (int)(slash - path);
            if (n >= (int)sizeof(dirpath)) return -1;
            for (int i = 0; i < n; i++) dirpath[i] = path[i];
            dirpath[n] = '\0';
            if (!resolve_path_dir(dirpath, &dir)) return -1;
        }
        fname = slash + 1;
    } else {
        dir = current_dir;
        fname = path;
    }
    if (*fname == '\0') return -1;

    uint32_t start, size;
    uint8_t attr;
    if (!dir_find(dir, fname, &start, &size, &attr)) return -1;
    if (attr & FAT_ATTR_DIRECTORY) return -1;     // it's a folder, not a file

    uint32_t limit  = size < max_bytes ? size : max_bytes;
    uint32_t copied = 0;
    uint8_t* dest   = (uint8_t*)buffer;
    uint8_t buf[512];

    uint32_t cluster = start;
    while (cluster < FAT_CLUSTER_EOC && copied < limit) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < sectors_per_cluster && copied < limit; s++) {
            if (!read_sectors(lba + s, buf)) return -1;
            for (int i = 0; i < 512 && copied < limit; i++) {
                dest[copied++] = buf[i];
            }
        }
        cluster = fat_next_cluster(cluster);
    }
    return (i32)copied;
}

// ---- VFS node interface ----
//
// A vnode's id is the object's first cluster (0 for an empty file), and aux
// says where its directory entry lives: the parent directory's first cluster
// in the high half, the entry's slot number within that directory in the low
// half. The root directory has no entry; its aux is FAT_AUX_ROOT.

#define FAT_AUX_ROOT  0xFFFFFFFFFFFFFFFFULL
#define CLUSTER_BYTES (sectors_per_cluster * 512)

static bool cluster_valid(uint32_t c) {
    return c >= 2 && c < FAT_CLUSTER_EOC;
}

// FAT's packed date and time (local time, 2-second steps; we write UTC).
static u64 fat_mtime(uint16_t date, uint16_t time) {
    if (date == 0) return 0;
    rtc_date_t d = {
        .year = 1980 + (date >> 9), .month = (date >> 5) & 0xF, .day = date & 0x1F,
        .hour = time >> 11, .min = (time >> 5) & 0x3F, .sec = (time & 0x1F) * 2,
    };
    if (d.month < 1 || d.month > 12 || d.day < 1) return 0;
    return rtc_from_date(&d);
}

static void node_from_entry(const dirent_t* e, uint32_t dir_cluster, uint32_t slot, vnode_t* out) {
    out->id   = ((uint32_t)e->cluster_hi << 16) | e->cluster_lo;
    out->aux  = ((u64)dir_cluster << 32) | slot;
    out->size = e->size;
    out->mtime = fat_mtime(e->write_date, e->write_time);
    out->type = (e->attr & FAT_ATTR_DIRECTORY) ? VN_DIR : VN_REG;
    if (out->type == VN_DIR && out->id == 0) out->id = root_cluster;   // ".." quirk
}

// Visit every slot of a directory until visit() returns true. Slots are
// numbered from 0 across the whole cluster chain, deleted ones included, so
// a slot number pins an entry's position on disk. Returns 1 if stopped by
// visit, 0 at the end of the directory, -EIO on a read error.
typedef bool (*slot_visit_t)(dirent_t* e, uint32_t slot, void* ctx);

static i32 dir_slots(uint32_t dir_cluster, slot_visit_t visit, void* ctx) {
    uint8_t buf[512];
    uint32_t slot = 0;
    uint32_t cluster = dir_cluster;
    while (cluster_valid(cluster)) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < sectors_per_cluster; s++) {
            if (!read_sectors(lba + s, buf)) return -EIO;
            dirent_t* e = (dirent_t*)buf;
            for (int i = 0; i < DIRENTS_PER_SECTOR; i++, slot++) {
                if (e[i].name[0] == FAT_DIRENT_END) return 0;
                if (visit(&e[i], slot, ctx)) return 1;
            }
        }
        cluster = fat_next_cluster(cluster);
    }
    return 0;
}

static bool is_dot_entry(const char* name) {
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

typedef struct {
    const char* want;       // lookup: name to match
    uint32_t index;         // readdir: which entry
    uint32_t seen;
    char* name_out;         // readdir: where the name goes
    uint32_t dir;
    vnode_t* out;
} slot_ctx_t;

static bool lookup_slot(dirent_t* e, uint32_t slot, void* ctx) {
    slot_ctx_t* c = (slot_ctx_t*)ctx;
    if (!fat_entry_usable(e)) return false;
    char name[13];
    fat_format_name(e->name, name);
    if (is_dot_entry(name) || !fat_name_eq(name, c->want)) return false;
    node_from_entry(e, c->dir, slot, c->out);
    return true;
}

static bool readdir_slot(dirent_t* e, uint32_t slot, void* ctx) {
    slot_ctx_t* c = (slot_ctx_t*)ctx;
    if (!fat_entry_usable(e)) return false;
    char name[13];
    fat_format_name(e->name, name);
    if (is_dot_entry(name) || c->seen++ != c->index) return false;
    for (int i = 0; i < 13; i++) c->name_out[i] = name[i];
    node_from_entry(e, c->dir, slot, c->out);
    return true;
}

static i32 fat_root(vnode_t* out) {
    out->id = root_cluster;
    out->aux = FAT_AUX_ROOT;
    out->size = 0;
    out->mtime = 0;
    out->type = VN_DIR;
    return 0;
}

static i32 fat_lookup(const vnode_t* dir, const char* name, vnode_t* out) {
    slot_ctx_t c = { .want = name, .dir = (uint32_t)dir->id, .out = out };
    i32 r = dir_slots((uint32_t)dir->id, lookup_slot, &c);
    if (r < 0) return r;
    return r == 1 ? 0 : -ENOENT;
}

static i32 fat_readdir(const vnode_t* dir, uint32_t index, char* name_out, vnode_t* out) {
    slot_ctx_t c = { .index = index, .name_out = name_out, .dir = (uint32_t)dir->id, .out = out };
    return dir_slots((uint32_t)dir->id, readdir_slot, &c);
}

static i64 fat_read(vnode_t* node, u64 off, void* buf, u64 len) {
    if (off >= node->size) return 0;
    if (len > node->size - off) len = node->size - off;

    uint32_t cluster = (uint32_t)node->id;
    for (u64 skip = off / CLUSTER_BYTES; skip; skip--) {
        if (!cluster_valid(cluster)) return -EIO;
        cluster = fat_next_cluster(cluster);
    }

    uint8_t sector[512];
    uint8_t* out = (uint8_t*)buf;
    u64 pos = off % CLUSTER_BYTES;          // offset within the current cluster
    u64 done = 0;
    while (done < len) {
        if (!cluster_valid(cluster)) return -EIO;
        uint32_t within = (uint32_t)(pos % 512);
        if (!read_sectors(cluster_to_lba(cluster) + (uint32_t)(pos / 512), sector)) return -EIO;
        u64 n = 512 - within;
        if (n > len - done) n = len - done;
        for (u64 i = 0; i < n; i++) out[done + i] = sector[within + i];
        done += n;
        pos += n;
        if (pos == CLUSTER_BYTES) {
            pos = 0;
            cluster = fat_next_cluster(cluster);
        }
    }
    return (i64)done;
}

// ---- writing ----
//
// FAT32 keeps a file as a singly linked list of clusters: the FAT holds one
// 28-bit "next" pointer per cluster (0 = free, >= EOC = last). Every FAT
// copy is updated, so tools that read the second one agree with us.
//
// Directory entries are rewritten in place whenever a file's size or first
// cluster changes. Only 8.3 names can be created; there is no long-name
// support yet.

// The current time in FAT's packed format: years from 1980, 2-second steps.
// Returned packed into one value (date << 16 | time), since dirent_t is a
// packed struct and its fields cannot safely be written through pointers.
static uint32_t fat_now(void) {
    rtc_date_t d;
    rtc_to_date(rtc_now(), &d);
    uint32_t y = d.year < 1980 ? 0 : d.year - 1980;
    uint32_t date = (y << 9) | (d.month << 5) | d.day;
    uint32_t time = (d.hour << 11) | (d.min << 5) | (d.sec / 2);
    return (date << 16) | time;
}

static uint32_t alloc_hint = 2;

static bool fat_set(uint32_t cluster, uint32_t value) {
    uint32_t offset = cluster * 4;
    uint8_t buf[512];
    for (uint32_t copy = 0; copy < fat_count; copy++) {
        uint32_t lba = fat_start + copy * fat_sectors + offset / 512;
        if (!read_sectors(lba, buf)) return false;
        uint32_t* entry = (uint32_t*)(buf + offset % 512);
        *entry = (*entry & ~FAT_ENTRY_MASK) | (value & FAT_ENTRY_MASK);  // keep the top 4 bits
        if (!write_sectors(lba, buf)) return false;
    }
    return true;
}

static bool zero_cluster(uint32_t cluster) {
    uint8_t zeros[512];
    memset(zeros, 0, sizeof(zeros));
    uint32_t lba = cluster_to_lba(cluster);
    for (uint32_t s = 0; s < sectors_per_cluster; s++) {
        if (!write_sectors(lba + s, zeros)) return false;
    }
    return true;
}

// FSInfo caches the free-cluster count. Rather than keep it exact, mark it
// "unknown" (0xFFFFFFFF, which the spec allows) the first time we allocate,
// so other systems recount instead of trusting a stale number.
static void fsinfo_invalidate(void) {
    static bool done = false;
    if (done || !fsinfo_sector) return;
    done = true;
    uint8_t buf[512];
    if (!read_sectors(fsinfo_sector, buf)) return;
    if (*(uint32_t*)buf != 0x41615252 || *(uint32_t*)(buf + 484) != 0x61417272) return;
    *(uint32_t*)(buf + 488) = 0xFFFFFFFF;    // free count: unknown
    *(uint32_t*)(buf + 492) = 0xFFFFFFFF;    // next free hint: unknown
    write_sectors(fsinfo_sector, buf);
}

// Find a free cluster, mark it as the end of a chain, zero it. 0 if full.
static uint32_t alloc_cluster(void) {
    fsinfo_invalidate();
    uint8_t buf[512];
    uint32_t last = total_clusters + 2;
    for (uint32_t pass = 0; pass < 2; pass++) {
        uint32_t c = pass == 0 ? alloc_hint : 2;
        uint32_t end = pass == 0 ? last : alloc_hint;
        uint32_t cached = 0xFFFFFFFF;
        for (; c < end; c++) {
            uint32_t sector = fat_start + (c * 4) / 512;
            if (sector != cached) {
                if (!read_sectors(sector, buf)) return 0;
                cached = sector;
            }
            if ((*(uint32_t*)(buf + (c * 4) % 512) & FAT_ENTRY_MASK) == 0) {
                if (!fat_set(c, FAT_ENTRY_MASK) || !zero_cluster(c)) return 0;
                alloc_hint = c + 1;
                return c;
            }
        }
    }
    return 0;
}

static void free_chain(uint32_t cluster) {
    while (cluster_valid(cluster)) {
        uint32_t next = fat_next_cluster(cluster);
        fat_set(cluster, 0);
        if (cluster < alloc_hint) alloc_hint = cluster;
        cluster = next;
    }
}

// Where slot `slot` of the directory starting at dir_cluster lives on disk.
static bool slot_location(uint32_t dir_cluster, uint32_t slot, uint32_t* lba, uint32_t* index) {
    uint32_t per_cluster = DIRENTS_PER_SECTOR * sectors_per_cluster;
    uint32_t cluster = dir_cluster;
    for (uint32_t skip = slot / per_cluster; skip; skip--) {
        if (!cluster_valid(cluster)) return false;
        cluster = fat_next_cluster(cluster);
    }
    if (!cluster_valid(cluster)) return false;
    uint32_t within = slot % per_cluster;
    *lba = cluster_to_lba(cluster) + within / DIRENTS_PER_SECTOR;
    *index = within % DIRENTS_PER_SECTOR;
    return true;
}

// Push a node's first cluster and size back into its directory entry.
static i32 dirent_sync(const vnode_t* node) {
    if (node->aux == FAT_AUX_ROOT) return 0;
    uint32_t lba, index;
    if (!slot_location((uint32_t)(node->aux >> 32), (uint32_t)node->aux, &lba, &index)) return -EIO;
    uint8_t buf[512];
    if (!read_sectors(lba, buf)) return -EIO;
    dirent_t* e = (dirent_t*)buf + index;
    e->cluster_hi = (uint16_t)(node->id >> 16);
    e->cluster_lo = (uint16_t)(node->id & 0xFFFF);
    if (node->type != VN_DIR) e->size = (uint32_t)node->size;
    uint32_t now = fat_now();
    e->write_date = e->access_date = (uint16_t)(now >> 16);
    e->write_time = (uint16_t)now;
    return write_sectors(lba, buf) ? 0 : -EIO;
}

// "readme.txt" -> "README  TXT". Fails for anything that is not a plain 8.3
// name made of characters FAT allows.
static i32 to_83(const char* name, uint8_t out[11]) {
    memset(out, ' ', 11);
    const char* dot = NULL;
    for (const char* p = name; *p; p++) if (*p == '.') { if (dot) return -EINVAL; dot = p; }
    uint32_t base_len = dot ? (uint32_t)(dot - name) : (uint32_t)strlen(name);
    uint32_t ext_len = dot ? (uint32_t)strlen(dot + 1) : 0;
    if (base_len == 0) return -EINVAL;
    if (base_len > 8 || ext_len > 3) return -ENAMETOOLONG;

    for (uint32_t i = 0; i < base_len + (dot ? 1 + ext_len : 0); i++) {
        char c = name[i];
        if (c == '.') continue;
        if (c >= 'a' && c <= 'z') c -= 32;
        bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  (c && strchr_k("!#$%&'()-@^_`{}~", c));
        if (!ok) return -EINVAL;
        if (i < base_len) out[i] = (uint8_t)c;
        else out[8 + (i - base_len - 1)] = (uint8_t)c;
    }
    return 0;
}

// Find a free slot in a directory, growing it by a cluster if it is full.
static i32 free_slot(uint32_t dir_cluster, uint32_t* slot_out) {
    uint8_t buf[512];
    uint32_t slot = 0;
    uint32_t cluster = dir_cluster, last = dir_cluster;
    while (cluster_valid(cluster)) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < sectors_per_cluster; s++) {
            if (!read_sectors(lba + s, buf)) return -EIO;
            dirent_t* e = (dirent_t*)buf;
            for (int i = 0; i < DIRENTS_PER_SECTOR; i++, slot++) {
                if (e[i].name[0] == FAT_DIRENT_END || e[i].name[0] == FAT_DIRENT_DELETED) {
                    *slot_out = slot;
                    return 0;
                }
            }
        }
        last = cluster;
        cluster = fat_next_cluster(cluster);
    }
    uint32_t grown = alloc_cluster();
    if (!grown) return -ENOSPC;
    if (!fat_set(last, grown)) return -EIO;
    *slot_out = slot;                // the first slot of the new cluster
    return 0;
}

static i32 write_slot(uint32_t dir_cluster, uint32_t slot, const dirent_t* entry) {
    uint32_t lba, index;
    if (!slot_location(dir_cluster, slot, &lba, &index)) return -EIO;
    uint8_t buf[512];
    if (!read_sectors(lba, buf)) return -EIO;
    ((dirent_t*)buf)[index] = *entry;
    return write_sectors(lba, buf) ? 0 : -EIO;
}

static void make_entry(dirent_t* e, const uint8_t name[11], uint8_t attr, uint32_t cluster) {
    memset(e, 0, sizeof(*e));
    for (int i = 0; i < 11; i++) e->name[i] = name[i];
    e->attr = attr;
    e->cluster_hi = (uint16_t)(cluster >> 16);
    e->cluster_lo = (uint16_t)(cluster & 0xFFFF);
    uint32_t now = fat_now();
    e->create_date = e->write_date = e->access_date = (uint16_t)(now >> 16);
    e->create_time = e->write_time = (uint16_t)now;
}

static i32 fat_create(const vnode_t* dir, const char* name, uint32_t type, vnode_t* out) {
    uint8_t short_name[11];
    i32 r = to_83(name, short_name);
    if (r < 0) return r;

    uint32_t dir_cluster = (uint32_t)dir->id;
    uint32_t slot;
    r = free_slot(dir_cluster, &slot);
    if (r < 0) return r;

    uint32_t first = 0;
    if (type == VN_DIR) {
        // A directory starts with "." (itself) and ".." (its parent, written
        // as 0 when the parent is the root -- a FAT quirk).
        first = alloc_cluster();
        if (!first) return -ENOSPC;
        dirent_t dots[2];
        uint8_t dot[11], dotdot[11];
        memset(dot, ' ', 11);
        memset(dotdot, ' ', 11);
        dot[0] = '.';
        dotdot[0] = dotdot[1] = '.';
        make_entry(&dots[0], dot, FAT_ATTR_DIRECTORY, first);
        make_entry(&dots[1], dotdot, FAT_ATTR_DIRECTORY, dir_cluster == root_cluster ? 0 : dir_cluster);
        if (write_slot(first, 0, &dots[0]) < 0 || write_slot(first, 1, &dots[1]) < 0) {
            free_chain(first);
            return -EIO;
        }
    }

    dirent_t e;
    make_entry(&e, short_name, type == VN_DIR ? FAT_ATTR_DIRECTORY : FAT_ATTR_ARCHIVE, first);
    r = write_slot(dir_cluster, slot, &e);
    if (r < 0) {
        if (first) free_chain(first);
        return r;
    }
    node_from_entry(&e, dir_cluster, slot, out);
    return 0;
}

// Cluster number `index` of a file's chain, allocating up to it if needed.
static uint32_t chain_get(vnode_t* node, uint64_t index, bool grow, bool* changed) {
    if (!cluster_valid((uint32_t)node->id)) {
        if (!grow) return 0;
        uint32_t c = alloc_cluster();
        if (!c) return 0;
        node->id = c;
        *changed = true;
    }
    uint32_t cluster = (uint32_t)node->id;
    for (uint64_t i = 0; i < index; i++) {
        uint32_t next = fat_next_cluster(cluster);
        if (!cluster_valid(next)) {
            if (!grow) return 0;
            next = alloc_cluster();
            if (!next || !fat_set(cluster, next)) return 0;
        }
        cluster = next;
    }
    return cluster;
}

static i64 write_range(vnode_t* node, u64 off, const uint8_t* src, u64 len, bool* changed) {
    uint8_t sector[512];
    u64 done = 0;
    while (done < len) {
        u64 pos = off + done;
        uint32_t cluster = chain_get(node, pos / CLUSTER_BYTES, true, changed);
        if (!cluster) return done ? (i64)done : -ENOSPC;
        u64 in_cluster = pos % CLUSTER_BYTES;
        uint32_t lba = cluster_to_lba(cluster) + (uint32_t)(in_cluster / 512);
        uint32_t within = (uint32_t)(in_cluster % 512);
        u64 n = 512 - within;
        if (n > len - done) n = len - done;
        // Partial sectors need the rest of the sector preserved.
        if (n != 512 && !read_sectors(lba, sector)) return -EIO;
        for (u64 i = 0; i < n; i++) sector[within + i] = src ? src[done + i] : 0;
        if (!write_sectors(lba, sector)) return -EIO;
        done += n;
    }
    return (i64)done;
}

static i64 fat_write(vnode_t* node, u64 off, const void* buf, u64 len) {
    if (off + len < off || off + len > 0xFFFFFFFFULL) return -EFBIG;  // FAT32's 4 GiB limit
    bool changed = false;
    // Writing past the end leaves a gap that must read back as zeros.
    if (off > node->size) {
        i64 z = write_range(node, node->size, NULL, off - node->size, &changed);
        if (z < 0) return z;
    }
    i64 n = write_range(node, off, (const uint8_t*)buf, len, &changed);
    if (n > 0 && off + (u64)n > node->size) {
        node->size = off + (u64)n;
        changed = true;
    }
    if (changed) {
        i32 r = dirent_sync(node);
        if (r < 0) return r;
    }
    return n;
}

static i32 fat_truncate(vnode_t* node, u64 size) {
    if (size > 0xFFFFFFFFULL) return -EFBIG;
    if (size >= node->size) {
        // Growing: zero-fill out to the new size.
        if (size > node->size) {
            bool changed = false;
            i64 z = write_range(node, node->size, NULL, size - node->size, &changed);
            if (z < 0) return (i32)z;
        }
    } else if (size == 0) {
        free_chain((uint32_t)node->id);
        node->id = 0;
    } else {
        // Keep just enough clusters for `size` bytes; free the rest.
        bool changed = false;
        uint32_t keep = (uint32_t)((size + CLUSTER_BYTES - 1) / CLUSTER_BYTES);
        uint32_t last = chain_get(node, keep - 1, false, &changed);
        if (!last) return -EIO;
        uint32_t rest = fat_next_cluster(last);
        if (!fat_set(last, FAT_ENTRY_MASK)) return -EIO;
        free_chain(rest);
    }
    node->size = size;
    return dirent_sync(node);
}

typedef struct {
    const char* want;
    uint32_t slot;
    dirent_t entry;
    bool found;
} find_ctx_t;

static bool find_slot(dirent_t* e, uint32_t slot, void* ctx) {
    find_ctx_t* f = (find_ctx_t*)ctx;
    if (!fat_entry_usable(e)) return false;
    char name[13];
    fat_format_name(e->name, name);
    if (is_dot_entry(name) || !fat_name_eq(name, f->want)) return false;
    f->slot = slot;
    f->entry = *e;
    f->found = true;
    return true;
}

static bool mark_deleted(uint32_t dir_cluster, uint32_t slot) {
    uint32_t lba, index;
    uint8_t buf[512];
    if (!slot_location(dir_cluster, slot, &lba, &index) || !read_sectors(lba, buf)) return false;
    ((dirent_t*)buf)[index].name[0] = FAT_DIRENT_DELETED;
    return write_sectors(lba, buf);
}

static i32 fat_unlink(const vnode_t* dir, const char* name) {
    uint32_t dir_cluster = (uint32_t)dir->id;
    find_ctx_t f = { .want = name };
    i32 r = dir_slots(dir_cluster, find_slot, &f);
    if (r < 0) return r;
    if (!f.found) return -ENOENT;

    vnode_t node;
    node_from_entry(&f.entry, dir_cluster, f.slot, &node);
    if (node.type == VN_DIR) {
        char tmp[VFS_NAME_MAX];
        vnode_t child;
        if (fat_readdir(&node, 0, tmp, &child) != 0) return -ENOTEMPTY;
    }

    if (!mark_deleted(dir_cluster, f.slot)) return -EIO;
    // Long-name fragments for this entry sit in the slots just before it.
    for (uint32_t s = f.slot; s > 0; s--) {
        uint32_t lba, index;
        uint8_t buf[512];
        if (!slot_location(dir_cluster, s - 1, &lba, &index) || !read_sectors(lba, buf)) break;
        dirent_t* e = (dirent_t*)buf + index;
        if (e->attr != FAT_ATTR_LONG_NAME || e->name[0] == FAT_DIRENT_DELETED) break;
        e->name[0] = FAT_DIRENT_DELETED;
        if (!write_sectors(lba, buf)) break;
    }
    free_chain((uint32_t)node.id);
    return 0;
}

// Is `cluster` the directory `dir` or one of its ancestors? Walks up the
// ".." entries from dir to the root.
static bool is_ancestor(uint32_t cluster, uint32_t dir) {
    for (int depth = 0; depth < 256; depth++) {
        if (dir == cluster) return true;
        if (dir == root_cluster) return false;
        // ".." is filtered out of normal lookups; it is always slot 1.
        uint8_t buf[512];
        if (!read_sectors(cluster_to_lba(dir), buf)) return false;
        dirent_t* e = (dirent_t*)buf + 1;
        if (e->name[0] != '.' || e->name[1] != '.') return false;
        uint32_t up = ((uint32_t)e->cluster_hi << 16) | e->cluster_lo;
        dir = up ? up : root_cluster;                         // 0 means the root
    }
    return true;                                              // absurdly deep: refuse
}

static i32 fat_rename(const vnode_t* olddir, const char* oldname,
                      const vnode_t* newdir, const char* newname) {
    uint8_t short_name[11];
    i32 r = to_83(newname, short_name);
    if (r < 0) return r;

    uint32_t from = (uint32_t)olddir->id, to = (uint32_t)newdir->id;
    find_ctx_t f = { .want = oldname };
    r = dir_slots(from, find_slot, &f);
    if (r < 0) return r;
    if (!f.found) return -ENOENT;
    bool is_dir = f.entry.attr & FAT_ATTR_DIRECTORY;
    uint32_t moving = ((uint32_t)f.entry.cluster_hi << 16) | f.entry.cluster_lo;
    if (is_dir && to != from && is_ancestor(moving, to)) return -EINVAL;  // into itself

    // Replace an existing file at the destination (the VFS vetted it).
    find_ctx_t g = { .want = newname };
    if (dir_slots(to, find_slot, &g) == 1 && g.found) {
        if (to == from && g.slot == f.slot) return 0;         // same entry
        r = fat_unlink(newdir, newname);
        if (r < 0) return r;
    }

    // New entry first, so a crash in between leaves two names, not none.
    uint32_t slot;
    r = free_slot(to, &slot);
    if (r < 0) return r;
    dirent_t e = f.entry;
    for (int i = 0; i < 11; i++) e.name[i] = short_name[i];
    r = write_slot(to, slot, &e);
    if (r < 0) return r;

    // The old slot may have moved if free_slot grew the same directory, but
    // slots only ever get appended, so f.slot still names the old entry.
    if (!mark_deleted(from, f.slot)) return -EIO;
    for (uint32_t s = f.slot; s > 0; s--) {                  // and its long name
        uint32_t lba, index;
        uint8_t buf[512];
        if (!slot_location(from, s - 1, &lba, &index) || !read_sectors(lba, buf)) break;
        dirent_t* lfn = (dirent_t*)buf + index;
        if (lfn->attr != FAT_ATTR_LONG_NAME || lfn->name[0] == FAT_DIRENT_DELETED) break;
        lfn->name[0] = FAT_DIRENT_DELETED;
        if (!write_sectors(lba, buf)) break;
    }

    // A directory that changed parents must have its ".." repointed.
    if (is_dir && to != from && cluster_valid(moving)) {
        uint32_t lba, index;
        uint8_t buf[512];
        if (slot_location(moving, 1, &lba, &index) && read_sectors(lba, buf)) {
            dirent_t* dd = (dirent_t*)buf + index;
            uint32_t parent = to == root_cluster ? 0 : to;
            dd->cluster_hi = (uint16_t)(parent >> 16);
            dd->cluster_lo = (uint16_t)(parent & 0xFFFF);
            write_sectors(lba, buf);
        }
    }
    return 0;
}

const vfs_ops_t fat32_vfs_ops = {
    .type = "fat32",
    .rename = fat_rename,
    .root = fat_root,
    .lookup = fat_lookup,
    .read = fat_read,
    .write = fat_write,
    .readdir = fat_readdir,
    .create = fat_create,
    .unlink = fat_unlink,
    .truncate = fat_truncate,
};
