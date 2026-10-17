// This file has been generated in whole or in part by a Large Language Model
// Copyright (c) 2026 Joseph Jones. Licensed under the Kunix License v1.0 or later.
// Offline growth for the little-endian, non-journaled makefs UFS2 geometry.
// The caller must exclusively own an unmounted target; this is not crash atomic.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#define UFS2_MAXFRAG 8
#define UFS2_MAGIC 0x19540119
#define UFS2_CG_MAGIC 0x090255

typedef struct {
    i32 cs_ndir;
    i32 cs_nbfree;
    i32 cs_nifree;
    i32 cs_nffree;
} ufs2_csum_t;

typedef struct {
    i64 cs_ndir;
    i64 cs_nbfree;
    i64 cs_nifree;
    i64 cs_nffree;
    i64 cs_numclusters;
    i64 cs_spare[3];
} ufs2_csum_total_t;

typedef struct {
    i32 fs_firstfield, fs_unused_1;
    i32 fs_sblkno, fs_cblkno, fs_iblkno, fs_dblkno;
    i32 fs_old_cgoffset, fs_old_cgmask, fs_old_time, fs_old_size, fs_old_dsize;
    u32 fs_ncg;
    i32 fs_bsize, fs_fsize, fs_frag;
    i32 fs_minfree, fs_old_rotdelay, fs_old_rps;
    i32 fs_bmask, fs_fmask, fs_bshift, fs_fshift;
    i32 fs_maxcontig, fs_maxbpg;
    i32 fs_fragshift, fs_fsbtodb, fs_sbsize;
    i32 fs_spare1[2];
    i32 fs_nindir;
    u32 fs_inopb;
    i32 fs_old_nspf;
    i32 fs_optim, fs_old_npsect, fs_old_interleave, fs_old_trackskew;
    i32 fs_id[2];
    i32 fs_old_csaddr, fs_cssize, fs_cgsize;
    i32 fs_spare2, fs_old_nsect, fs_old_spc, fs_old_ncyl, fs_old_cpg;
    u32 fs_ipg;
    i32 fs_fpg;
    ufs2_csum_t fs_old_cstotal;
    i8  fs_fmod, fs_clean, fs_ronly, fs_old_flags;
    u8  fs_fsmnt[468];
    u8  fs_volname[32];
    u64 fs_swuid;
    i32 fs_pad;
    i32 fs_cgrotor;
    u8  fs_incore_ptrs[128];      // pointers FreeBSD keeps in core; 0 on disk
    i32 fs_old_cpc, fs_maxbsize;
    i64 fs_unrefs, fs_providersize, fs_metaspace;
    i64 fs_sparecon64[14];
    i64 fs_sblockloc;
    ufs2_csum_total_t fs_cstotal;
    i64 fs_time, fs_size, fs_dsize, fs_csaddr, fs_pendingblocks;
    u32 fs_pendinginodes;
    u32 fs_snapinum[20];
    u32 fs_avgfilesize, fs_avgfpdir;
    i32 fs_save_cgsize;
    i64 fs_mtime;
    i32 fs_sujfree;
    i32 fs_sparecon32[23];
    i32 fs_flags, fs_contigsumsize, fs_maxsymlinklen, fs_old_inodefmt;
    u64 fs_maxfilesize;
    i64 fs_qbmask, fs_qfmask;
    i32 fs_state, fs_old_postblformat, fs_old_nrpos;
    i32 fs_spare5[2];
    i32 fs_magic;
} ufs2_fs_t;

_Static_assert(sizeof(ufs2_fs_t) == 1376, "struct fs is 1376 bytes");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_ipg) == 184, "fs_ipg");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_clean) == 209, "fs_clean");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_cgrotor) == 724, "fs_cgrotor");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_old_cpc) == 856, "fs_old_cpc");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_sblockloc) == 1000, "fs_sblockloc");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_cstotal) == 1008, "fs_cstotal");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_csaddr) == 1096, "fs_csaddr");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_avgfilesize) == 1196, "fs_avgfilesize");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_flags) == 1312, "fs_flags");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_maxfilesize) == 1328, "fs_maxfilesize");
_Static_assert(__builtin_offsetof(ufs2_fs_t, fs_magic) == 1372, "fs_magic");

// Cylinder group header; its maps follow at the offsets it records.
typedef struct {
    i32 cg_firstfield, cg_magic, cg_old_time;
    u32 cg_cgx;
    i16 cg_old_ncyl, cg_old_niblk;
    u32 cg_ndblk;                 // fragments in this group
    ufs2_csum_t cg_cs;
    u32 cg_rotor, cg_frotor, cg_irotor;
    u32 cg_frsum[UFS2_MAXFRAG];   // [n]: free runs of exactly n fragments
    i32 cg_old_btotoff, cg_old_boff;
    u32 cg_iusedoff;              // u8 bitmap: inode in use
    u32 cg_freeoff;               // u8 bitmap: fragment free
    u32 cg_nextfreeoff;
    u32 cg_clustersumoff;         // u32 counts of free block runs, by length
    u32 cg_clusteroff;            // u8 bitmap: block entirely free
    u32 cg_nclusterblks, cg_niblk, cg_initediblk, cg_unrefs;
    i32 cg_sparecon32[2];
    i64 cg_time;
    i64 cg_sparecon64[3];
} ufs2_cg_t;

_Static_assert(sizeof(ufs2_cg_t) == 168, "cg header is 168 bytes");
_Static_assert(__builtin_offsetof(ufs2_cg_t, cg_frsum) == 52, "cg_frsum");
_Static_assert(__builtin_offsetof(ufs2_cg_t, cg_iusedoff) == 92, "cg_iusedoff");
_Static_assert(__builtin_offsetof(ufs2_cg_t, cg_initediblk) == 120, "cg_initediblk");
_Static_assert(__builtin_offsetof(ufs2_cg_t, cg_time) == 136, "cg_time");

static int fd;
static ufs2_fs_t fs;
static u8 sb[8192];
static u8 **groups;
static u64 sbloc;

static void fail(const char *why) {
    fprintf(stderr, "ufsgrow: %s\n", why);
    exit(1);
}
static void io(int writing, void *buf, size_t count, u64 offset) {
    while (count) {
        ssize_t n = writing ? pwrite(fd, buf, count, (off_t)offset)
                            : pread(fd, buf, count, (off_t)offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) fail(writing ? "write failed; run fsck before mounting" : "short read or I/O error");
        count -= n;
        offset += n;
        buf = (u8 *)buf + n;
    }
}
static int bit(const u8 *p, u32 n) { return (p[n / 8] >> (n % 8)) & 1; }
static void set(u8 *p, u32 n, int value) {
    if (value) p[n / 8] |= 1u << (n % 8);
    else p[n / 8] &= ~(1u << (n % 8));
}
static ufs2_cg_t *cg(u32 c) { return (ufs2_cg_t *)groups[c]; }
static u64 address(u32 c, u32 rel) { return ((u64)c * fs.fs_fpg + rel) * fs.fs_fsize; }
static u64 roundup(u64 n, u64 a) { return (n + a - 1) / a * a; }
static int power2(u32 n) { return n && !(n & (n - 1)); }

// Derive summaries from the bitmap, including the final partial block.
static void recount(ufs2_cg_t *g) {
    u8 *map = (u8 *)g + g->cg_freeoff;
    u8 *clusters = (u8 *)g + g->cg_clusteroff;
    u32 *sums = (u32 *)((u8 *)g + g->cg_clustersumoff);
    u32 contig = fs.fs_contigsumsize;
    memset(g->cg_frsum, 0, sizeof g->cg_frsum);
    if (contig) {
        memset(clusters, 0, (fs.fs_fpg / fs.fs_frag + 7) / 8);
        memset(sums + 1, 0, contig * 4);
    }
    g->cg_cs.cs_nbfree = g->cg_cs.cs_nffree = 0;
    g->cg_nclusterblks = g->cg_ndblk / fs.fs_frag;
    for (u32 b = 0; b < g->cg_ndblk; b += fs.fs_frag) {
        u32 end = b + fs.fs_frag, freecount = 0, run = 0;
        if (end > g->cg_ndblk) end = g->cg_ndblk;
        for (u32 j = b; j < end; j++) freecount += bit(map, j);
        if (freecount == (u32)fs.fs_frag) {
            g->cg_cs.cs_nbfree++;
            if (contig) set(clusters, b / fs.fs_frag, 1);
        } else {
            g->cg_cs.cs_nffree += freecount;
            for (u32 j = b; j <= end; j++) {
                if (j < end && bit(map, j)) { run++; continue; }
                if (run) g->cg_frsum[run]++;
                run = 0;
            }
        }
    }
    if (contig) {
        u32 run = 0;
        for (u32 i = 0; i <= g->cg_nclusterblks; i++) {
            if (i < g->cg_nclusterblks && bit(clusters, i)) { run++; continue; }
            if (run) sums[run > contig ? contig : run]++;
            run = 0;
        }
    }
}
static void valid_group(ufs2_cg_t *g, u32 c, u32 expected) {
    u64 n = fs.fs_cgsize, inodebytes = (fs.fs_ipg + 7ULL) / 8;
    u64 fragend = (u64)g->cg_freeoff + (fs.fs_fpg + 7ULL) / 8;
    if (expected < (u32)fs.fs_dblkno || g->cg_magic != UFS2_CG_MAGIC || g->cg_cgx != c || g->cg_ndblk != expected ||
        g->cg_iusedoff < sizeof *g || g->cg_iusedoff + inodebytes > g->cg_freeoff ||
        fragend > n || g->cg_nextfreeoff > n || g->cg_niblk != fs.fs_ipg ||
        g->cg_initediblk > fs.fs_ipg || g->cg_initediblk % fs.fs_inopb || g->cg_unrefs ||
        g->cg_cs.cs_ndir < 0 || g->cg_cs.cs_ndir > (i32)fs.fs_ipg ||
        (fs.fs_contigsumsize &&
         (g->cg_clustersumoff % 4 || g->cg_clustersumoff + 4ULL < fragend ||
          g->cg_clusteroff < g->cg_clustersumoff + 4ULL * (fs.fs_contigsumsize + 1) ||
          (u64)g->cg_clusteroff + (fs.fs_fpg / fs.fs_frag + 7ULL) / 8 > n ||
          g->cg_nclusterblks != expected / fs.fs_frag))) fail("corrupt cylinder group geometry");
    u8 *map = (u8 *)g + g->cg_freeoff;
    for (u32 j = c ? fs.fs_sblkno : 0; j < (u32)fs.fs_dblkno; j++)
        if (bit(map, j)) fail("metadata marked free");
    for (u32 j = expected; j < (u32)fs.fs_fpg; j++)
        if (bit(map, j)) fail("out-of-range fragments marked free");
    u32 used = 0;
    for (u32 j = 0; j < fs.fs_ipg; j++) {
        if (bit((u8 *)g + g->cg_iusedoff, j)) {
            if (j >= g->cg_initediblk) fail("allocated inode is uninitialized");
            used++;
        }
    }
    if (g->cg_cs.cs_nifree != (i32)(fs.fs_ipg - used)) fail("inode summary mismatch");
    if (!c && (!bit((u8 *)g + g->cg_iusedoff, 0) || !bit((u8 *)g + g->cg_iusedoff, 1)))
        fail("reserved inodes marked free");
    u8 *copy = malloc(n);
    if (!copy) fail("out of memory");
    memcpy(copy, g, n);
    recount((ufs2_cg_t *)copy);
    ufs2_cg_t *checked = (ufs2_cg_t *)copy;
    if (memcmp(&checked->cg_cs, &g->cg_cs, sizeof g->cg_cs) ||
        memcmp(checked->cg_frsum, g->cg_frsum, sizeof g->cg_frsum) ||
        (fs.fs_contigsumsize &&
         (memcmp(copy + g->cg_clustersumoff + 4, (u8 *)g + g->cg_clustersumoff + 4, fs.fs_contigsumsize * 4) ||
          memcmp(copy + g->cg_clusteroff, (u8 *)g + g->cg_clusteroff, (g->cg_nclusterblks + 7) / 8))))
        fail("allocation summary mismatch; run fsck");
    free(copy);
}

// Independently account for every allocated fragment. Consistent summaries alone
// cannot detect a damaged inode pointing into metadata or another file's block.
static u8 *claimed;
static u64 audit_size;
static void claim(u64 f, u32 count) {
    if (!f || !count || f >= audit_size || count > audit_size - f)
        fail("inode block outside filesystem");
    for (u32 j = 0; j < count; j++) {
        u64 addr = f + j;
        u32 c = addr / fs.fs_fpg, rel = addr % fs.fs_fpg;
        if (bit(claimed, addr) || bit(groups[c] + cg(c)->cg_freeoff, rel))
            fail("duplicate, metadata, or free inode block");
        set(claimed, addr, 1);
    }
}
static u64 indirect(u64 f, u32 level) {
    if (f % fs.fs_frag) fail("unaligned indirect block");
    claim(f, fs.fs_frag);
    u64 held = fs.fs_frag;
    i64 *ptrs = malloc(fs.fs_bsize);
    if (!ptrs) fail("out of memory");
    io(0, ptrs, fs.fs_bsize, f * fs.fs_fsize);
    for (u32 j = 0; j < (u32)fs.fs_nindir; j++) {
        if (!ptrs[j]) continue;
        if (level) held += indirect(ptrs[j], level - 1);
        else {
            if (ptrs[j] % fs.fs_frag) fail("unaligned indirect data block");
            claim(ptrs[j], fs.fs_frag); held += fs.fs_frag;
        }
    }
    free(ptrs);
    return held;
}
static void audit(void) {
    audit_size = fs.fs_size;
    if ((audit_size + 7) / 8 > 256ULL * 1024 * 1024) fail("filesystem audit exceeds memory limit");
    claimed = calloc(1, (audit_size + 7) / 8);
    u8 *table = malloc(fs.fs_bsize);
    if (!claimed || !table) fail("out of memory");
    for (u32 c = 0; c < fs.fs_ncg; c++)
        for (u32 j = c ? fs.fs_sblkno : 0; j < (u32)fs.fs_dblkno; j++)
            set(claimed, (u64)c * fs.fs_fpg + j, 1);
    claim(fs.fs_csaddr, fs.fs_cssize / fs.fs_fsize);
    for (u32 c = 0; c < fs.fs_ncg; c++) {
        ufs2_cg_t *g = cg(c);
        u32 dirs = 0;
        for (u32 first = 0; first < g->cg_initediblk; first += fs.fs_inopb) {
            io(0, table, fs.fs_bsize, address(c, fs.fs_iblkno) + (u64)first * 256);
            for (u32 j = 0; j < fs.fs_inopb; j++) {
                u32 index = first + j;
                if (!c && index < 2) continue;
                u8 *d = table + j * 256;
                u16 mode;
                u32 extsize;
                u64 filesize, sectors;
                i64 db[12], ib[3], ext[2];
                memcpy(&mode, d, 2);
                if ((mode != 0) != bit(groups[c] + g->cg_iusedoff, index))
                    fail("inode allocation map disagrees with inode table");
                if (!mode) continue;
                u16 type = mode & 0170000;
                if (type == 0040000) dirs++;
                memcpy(&filesize, d + 16, 8);
                memcpy(&sectors, d + 24, 8);
                memcpy(&extsize, d + 92, 4);
                memcpy(ext, d + 96, sizeof ext);
                memcpy(db, d + 112, sizeof db);
                memcpy(ib, d + 208, sizeof ib);
                if (extsize || ext[0] || ext[1]) fail("extended inode blocks are unsupported");
                if (type == 0120000 && !sectors) {
                    if (filesize > 120) fail("invalid inline symlink");
                    continue;
                }
                if (type == 0020000 || type == 0060000 || type == 0010000 || type == 0140000) {
                    if (sectors) fail("special inode owns data blocks");
                    continue;
                }
                if (type != 0100000 && type != 0040000 && type != 0120000)
                    fail("unsupported inode type");
                u64 held = 0;
                for (u32 k = 0; k < 12; k++) {
                    if (!db[k]) continue;
                    u64 begin = (u64)k * fs.fs_bsize;
                    if (begin >= filesize) fail("inode direct block beyond EOF");
                    u64 length = filesize - begin;
                    u32 n = length >= (u32)fs.fs_bsize ? (u32)fs.fs_frag : roundup(length, fs.fs_fsize) / fs.fs_fsize;
                    if ((u64)db[k] % fs.fs_frag + n > (u32)fs.fs_frag)
                        fail("inode fragments cross a block boundary");
                    claim(db[k], n);
                    held += n;
                }
                for (u32 k = 0; k < 3; k++) if (ib[k]) held += indirect(ib[k], k);
                if (held * (fs.fs_fsize / 512) != sectors) fail("inode block count mismatch");
            }
        }
        if (dirs != (u32)g->cg_cs.cs_ndir) fail("directory summary mismatch");
        for (u32 j = 0; j < g->cg_ndblk; j++)
            if (bit(claimed, (u64)c * fs.fs_fpg + j) == bit(groups[c] + g->cg_freeoff, j))
                fail("unaccounted allocated fragment");
    }
    free(table);
    free(claimed);
}

int main(int argc, char **argv) {
    if (argc != 3) fail("usage: ufsgrow DEVICE SIZE_BYTES (unmounted target only)");
    if (!argv[2][0]) fail("invalid byte size");
    for (char *p = argv[2]; *p; p++) if (*p < '0' || *p > '9') fail("invalid byte size");
    errno = 0;
    u64 bytes = strtoull(argv[2], NULL, 10), capacity = 0;
    if (errno || !bytes || bytes > INT64_MAX) fail("invalid byte size");
    fd = open(argv[1], O_RDWR | O_EXCL);
    if (fd < 0) fail("cannot open target exclusively");
    struct stat st;
    if (fstat(fd, &st)) fail("cannot stat target");
    if (S_ISREG(st.st_mode)) capacity = st.st_size;
    else if (!S_ISBLK(st.st_mode) || ioctl(fd, 0x80081272UL, &capacity)) fail("cannot determine device capacity");
    if (bytes > capacity) fail("requested size exceeds target capacity");
    static const u64 locations[] = {65536, 8192, 0, 262144};
    for (u32 i = 0; i < sizeof locations / sizeof *locations; i++) {
        if (locations[i] + sizeof sb > capacity) continue;
        io(0, sb, sizeof sb, locations[i]);
        memcpy(&fs, sb, sizeof fs);
        if (fs.fs_magic == UFS2_MAGIC) { sbloc = locations[i]; break; }
    }
    if (fs.fs_magic != UFS2_MAGIC) fail("no little-endian UFS2 superblock");
    if (!power2(fs.fs_bsize) || fs.fs_bsize < 4096 || fs.fs_bsize > 65536 ||
        !power2(fs.fs_fsize) || fs.fs_fsize < 512 || fs.fs_fsize > fs.fs_bsize ||
        fs.fs_bshift < 0 || fs.fs_bshift > 16 || (1U << fs.fs_bshift) != (u32)fs.fs_bsize ||
        fs.fs_fshift < 9 || fs.fs_fshift > 16 || (1U << fs.fs_fshift) != (u32)fs.fs_fsize ||
        fs.fs_fragshift < 0 || fs.fs_fragshift > 3 || (1U << fs.fs_fragshift) != (u32)fs.fs_frag ||
        fs.fs_fsbtodb != fs.fs_fshift - 9 || fs.fs_bmask != ~(fs.fs_bsize - 1) ||
        fs.fs_fmask != ~(fs.fs_fsize - 1) || fs.fs_qbmask != fs.fs_bsize - 1 ||
        fs.fs_qfmask != fs.fs_fsize - 1 || fs.fs_sblockloc != (i64)sbloc ||
        sbloc + sizeof sb > (u64)fs.fs_cblkno * fs.fs_fsize ||
        fs.fs_clean < 0 || fs.fs_clean > 1 || fs.fs_fmod ||
        fs.fs_frag != fs.fs_bsize / fs.fs_fsize || fs.fs_frag > 8 ||
        fs.fs_fpg <= 0 || fs.fs_fpg % fs.fs_frag || !fs.fs_ipg ||
        fs.fs_nindir != fs.fs_bsize / 8 ||
        fs.fs_inopb != (u32)fs.fs_bsize / 256 || fs.fs_ipg % fs.fs_inopb ||
        fs.fs_cgsize < (i32)sizeof(ufs2_cg_t) || fs.fs_cgsize > fs.fs_bsize ||
        fs.fs_sbsize < (i32)sizeof fs || fs.fs_sbsize > (i32)sizeof sb ||
        fs.fs_sblkno < 0 || fs.fs_cblkno < (i64)fs.fs_sblkno + (i64)roundup(8192, fs.fs_fsize) / fs.fs_fsize ||
        fs.fs_iblkno < (i64)fs.fs_cblkno + (i64)roundup(fs.fs_cgsize, fs.fs_fsize) / fs.fs_fsize ||
        fs.fs_dblkno != fs.fs_iblkno + (i64)fs.fs_ipg * 256 / fs.fs_fsize ||
        fs.fs_dblkno >= fs.fs_fpg || fs.fs_size <= 0 ||
        fs.fs_contigsumsize < 0 || fs.fs_contigsumsize > 64 ||
        fs.fs_flags || fs.fs_pendingblocks || fs.fs_pendinginodes || fs.fs_unrefs ||
        (!fs.fs_clean && fs.fs_fsmnt[0])) fail("unsupported or dirty filesystem geometry/features");
    for (u32 i = 0; i < 20; i++) if (fs.fs_snapinum[i]) fail("snapshots are unsupported");
    u64 oldsize = fs.fs_size, size = bytes / fs.fs_bsize * fs.fs_frag;
    if (size < oldsize) fail("shrinking is unsupported");
    if (oldsize > capacity / fs.fs_fsize || fs.fs_ncg != (oldsize + fs.fs_fpg - 1) / fs.fs_fpg ||
        fs.fs_cssize <= 0 || fs.fs_cssize % fs.fs_fsize ||
        (u64)fs.fs_cssize < fs.fs_ncg * sizeof(ufs2_csum_t) || fs.fs_csaddr < 0 ||
        (u64)fs.fs_csaddr + fs.fs_cssize / fs.fs_fsize > oldsize) fail("invalid superblock bounds");
    u64 ncg64 = (size + fs.fs_fpg - 1) / fs.fs_fpg;
    if (!ncg64 || ncg64 > UINT32_MAX || ncg64 * fs.fs_ipg > UINT32_MAX ||
        ncg64 * fs.fs_cgsize > 256ULL * 1024 * 1024 ||
        (size % fs.fs_fpg && size % fs.fs_fpg < (u32)fs.fs_dblkno)) fail("target geometry exceeds supported limits");
    u32 oldncg = fs.fs_ncg, ncg = ncg64;
    groups = calloc(ncg, sizeof *groups);
    if (!groups) fail("out of memory");
    ufs2_csum_t *oldsum = malloc(fs.fs_cssize);
    if (!oldsum) fail("out of memory");
    io(0, oldsum, fs.fs_cssize, (u64)fs.fs_csaddr * fs.fs_fsize);
    ufs2_csum_total_t totals = {0};
    for (u32 c = 0; c < ncg; c++) {
        groups[c] = calloc(1, fs.fs_cgsize);
        if (!groups[c]) fail("out of memory");
        if (c < oldncg) {
            io(0, groups[c], fs.fs_cgsize, address(c, fs.fs_cblkno));
            u32 expected = oldsize - (u64)c * fs.fs_fpg;
            if (oldsize - (u64)c * fs.fs_fpg > (u32)fs.fs_fpg) expected = fs.fs_fpg;
            valid_group(cg(c), c, expected);
            if (memcmp(&oldsum[c], &cg(c)->cg_cs, sizeof *oldsum)) fail("group summary array mismatch");
            totals.cs_ndir += cg(c)->cg_cs.cs_ndir;
            totals.cs_nbfree += cg(c)->cg_cs.cs_nbfree;
            totals.cs_nifree += cg(c)->cg_cs.cs_nifree;
            totals.cs_nffree += cg(c)->cg_cs.cs_nffree;
        }
    }
    if (memcmp(&totals, &fs.fs_cstotal, 5 * sizeof(i64))) fail("superblock totals mismatch");
    audit();
    if (size == oldsize) { puts("ufsgrow: filesystem already has requested size"); return 0; }
    u64 oldcs = fs.fs_csaddr;
    u32 oldcsfrags = fs.fs_cssize / fs.fs_fsize;
    // The old summary must occupy data fragments which are currently allocated.
    for (u32 i = 0; i < oldcsfrags; i++) {
        u64 f = oldcs + i;
        u32 c = f / fs.fs_fpg, rel = f % fs.fs_fpg;
        if (rel < (u32)fs.fs_dblkno || bit(groups[c] + cg(c)->cg_freeoff, rel)) fail("invalid summary allocation");
    }
    ufs2_cg_t prototype = *cg(0);
    fs.fs_dsize = 0;
    for (u32 c = 0; c < ncg; c++) {
        ufs2_cg_t *g = cg(c);
        u32 previous = c < oldncg ? g->cg_ndblk : 0;
        if (c >= oldncg) {
            *g = prototype;
            g->cg_cgx = c;
            g->cg_rotor = g->cg_frotor = g->cg_irotor = 0;
            g->cg_cs = (ufs2_csum_t){.cs_nifree = fs.fs_ipg};
            g->cg_initediblk = fs.fs_ipg < 2 * fs.fs_inopb ? fs.fs_ipg : 2 * fs.fs_inopb;
        }
        u64 remain = size - (u64)c * fs.fs_fpg;
        g->cg_ndblk = remain < (u32)fs.fs_fpg ? remain : (u32)fs.fs_fpg;
        u8 *map = groups[c] + g->cg_freeoff;
        for (u32 i = previous; i < g->cg_ndblk; i++)
            if (i < (u32)fs.fs_sblkno || i >= (u32)fs.fs_dblkno) set(map, i, 1);
        fs.fs_dsize += g->cg_ndblk - fs.fs_dblkno + (c ? fs.fs_sblkno : 0);
    }
    for (u32 i = 0; i < oldcsfrags; i++) {
        u64 f = oldcs + i;
        set(groups[f / fs.fs_fpg] + cg(f / fs.fs_fpg)->cg_freeoff, f % fs.fs_fpg, 1);
    }
    u64 cssize = roundup((u64)ncg * sizeof(ufs2_csum_t), fs.fs_fsize);
    if (cssize > INT32_MAX) fail("summary array too large");
    u32 csfrags = cssize / fs.fs_fsize;
    u64 csaddr = 0;
    // A whole contiguous extent prevents splitting the summary across metadata.
    for (u32 c = 0; c < ncg && !csaddr; c++) {
        u8 *map = groups[c] + cg(c)->cg_freeoff;
        u32 run = 0;
        for (u32 i = fs.fs_dblkno; i < cg(c)->cg_ndblk; i++) {
            run = bit(map, i) ? run + 1 : 0;
            if (run >= csfrags && (i + 1 - csfrags) % fs.fs_frag == 0) {
                u32 start = i + 1 - csfrags;
                csaddr = (u64)c * fs.fs_fpg + start;
                for (u32 j = 0; j < csfrags; j++) set(map, start + j, 0);
                break;
            }
        }
    }
    if (!csaddr) fail("no contiguous extent for grown summary array");
    u8 *summary = calloc(1, cssize), *zeros = calloc(2, fs.fs_bsize);
    if (!summary || !zeros) fail("out of memory");
    fs.fs_ncg = ncg;
    fs.fs_size = size;
    fs.fs_providersize = capacity / fs.fs_fsize;
    fs.fs_csaddr = csaddr;
    fs.fs_cssize = cssize;
    fs.fs_dsize -= csfrags;
    memset(&fs.fs_cstotal, 0, sizeof fs.fs_cstotal);
    for (u32 c = 0; c < ncg; c++) {
        recount(cg(c));
        ((ufs2_csum_t *)summary)[c] = cg(c)->cg_cs;
        fs.fs_cstotal.cs_ndir += cg(c)->cg_cs.cs_ndir;
        fs.fs_cstotal.cs_nbfree += cg(c)->cg_cs.cs_nbfree;
        fs.fs_cstotal.cs_nifree += cg(c)->cg_cs.cs_nifree;
        fs.fs_cstotal.cs_nffree += cg(c)->cg_cs.cs_nffree;
    }
    // Complete all validation and allocation before the first write. An interrupted
    // operation is marked dirty; fsck is required before the filesystem is reused.
    ufs2_fs_t dirty;
    memcpy(&dirty, sb, sizeof dirty);
    dirty.fs_clean = 0;
    dirty.fs_flags |= 4; // FS_NEEDSFSCK also rejects interrupted retries.
    memcpy(sb, &dirty, sizeof dirty);
    io(1, sb, sizeof sb, sbloc);
    if (fsync(fd)) fail("flush failed");
    for (u32 c = oldncg; c < ncg; c++)
        io(1, zeros, (size_t)cg(c)->cg_initediblk * 256, address(c, fs.fs_iblkno));
    for (u32 c = 0; c < ncg; c++) io(1, groups[c], fs.fs_cgsize, address(c, fs.fs_cblkno));
    io(1, summary, cssize, csaddr * fs.fs_fsize);
    if (fsync(fd)) fail("flush failed");
    fs.fs_clean = 1;
    memcpy(sb, &fs, sizeof fs);
    for (u32 c = 0; c < ncg; c++) io(1, sb, sizeof sb, address(c, fs.fs_sblkno));
    if (fsync(fd)) fail("flush failed");
    io(1, sb, sizeof sb, sbloc);
    if (fsync(fd)) fail("flush failed");
    printf("ufsgrow: grew to %" PRIu64 " bytes (%u cylinder groups)\n", size * fs.fs_fsize, ncg);
    close(fd);
    return 0;
}
