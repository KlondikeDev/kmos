// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// This file has been generated in whole or in part by a Large Language Model

#include <types.h>
#include <fat32.h>
#include <ata.h>
#include <console.h>
#include <klibc/string.h>

_Static_assert(sizeof(bpb_t) == 512, "bpb_t must be exactly one sector");
_Static_assert(sizeof(dirent_t) == 32, "dirent_t must be exactly 32 bytes");

// Filesystem geometry, cached at mount by fat32_init().
static uint32_t fat_start;            // first FAT, in sectors
static uint32_t data_start;           // first data sector
static uint32_t sectors_per_cluster;
static uint32_t root_cluster;

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
