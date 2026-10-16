#!/usr/bin/env python3
# This file has been generated in whole or in part by a Large Language Model

# Copyright (c) 2026 Joseph Jones
# Licensed under the Kunix License v1.0 or later
# See LICENSE for details

"""Explain what fsck_ffs would complain about in a UFS2 image.

fsck says *that* the maps or summaries are wrong; this says *where*: it
walks every inode's blocks, rebuilds each cylinder group's maps and counts
the way fsck's pass 5 does, and prints every difference from the disk.

    python3 buildutils/ufsdiag.py ufs.img
"""

import struct
import sys

SBLOCK_SEARCH = [65536, 8192, 0, 262144]
NDADDR, NIADDR = 12, 3


def main(path):
    img = open(path, "rb").read()
    for loc in SBLOCK_SEARCH:
        if struct.unpack_from("<i", img, loc + 1372)[0] == 0x19540119:
            break
    else:
        sys.exit("no UFS2 superblock")
    sb = lambda off, fmt="<i": struct.unpack_from(fmt, img, loc + off)[0]
    cblkno, iblkno = sb(12), sb(16)
    ncg, bsize, fsize, frag = sb(44, "<I"), sb(48), sb(52), sb(56)
    ipg, fpg = sb(184, "<I"), sb(188)
    nindir, inopb = sb(116), sb(120, "<I")
    fs_size, csaddr, cgsize = sb(1080, "<q"), sb(1096, "<q"), sb(160)
    contig = sb(1316)
    total = struct.unpack_from("<5q", img, loc + 1008)
    problems = 0

    def say(msg):
        nonlocal problems
        problems += 1
        print(msg)

    def inode(ino):
        c, idx = divmod(ino, ipg)
        f = c * fpg + iblkno + (idx // inopb) * frag
        off = f * fsize + (idx % inopb) * 256
        return img[off:off + 256]

    used = {}                                # fragment -> inode using it

    def claim(f, n, ino, what):
        for k in range(n):
            if f + k in used:
                say(f"frag {f + k} used twice: inode {used[f + k]} and {ino} ({what})")
            used[f + k] = ino

    # The cylinder group summary array is in use, though no inode owns it.
    claim(csaddr, -(-sb(156) // fsize), "summary", "fs_csaddr")

    alloc_inodes, dirs = set(), set()
    for ino in range(ncg * ipg):
        d = inode(ino)
        mode, nlink = struct.unpack_from("<Hh", d, 0)
        if ino < 2 or mode == 0:
            continue
        alloc_inodes.add(ino)
        if mode & 0o170000 == 0o040000:
            dirs.add(ino)
        size, blocks = struct.unpack_from("<QQ", d, 16)
        db = struct.unpack_from("<12q", d, 112)
        ib = struct.unpack_from("<3q", d, 208)
        held = 0
        if mode & 0o170000 == 0o120000 and blocks == 0:
            continue                         # short symlink: target in db/ib
        last = (size - 1) // bsize if size else -1
        for lbn, f in enumerate(db):
            if not f:
                continue
            if lbn < last or lbn > last:
                n = frag
                if lbn > last:
                    say(f"inode {ino}: direct block {lbn} beyond the size ({size})")
            else:
                n = -(-(size - lbn * bsize) // fsize)
            claim(f, n, ino, f"db[{lbn}]")
            held += n

        def walk(f, level):
            nonlocal held
            claim(f, frag, ino, f"indirect level {level}")
            held += frag
            ptrs = struct.unpack_from(f"<{nindir}q", img, f * fsize)
            for p in ptrs:
                if not p:
                    continue
                if level == 0:
                    claim(p, frag, ino, "data via indirect")
                    held += frag
                else:
                    walk(p, level - 1)

        for level, f in enumerate(ib):
            if f:
                walk(f, level)
        if held * (fsize // 512) != blocks:
            say(f"inode {ino}: di_blocks {blocks}, but holds {held * (fsize // 512)} sectors")

    sums = [0, 0, 0, 0]
    for c in range(ncg):
        base = (c * fpg + cblkno) * fsize
        cg = img[base:base + cgsize]
        magic, cgx, ndblk = struct.unpack_from("<i", cg, 4)[0], struct.unpack_from("<I", cg, 12)[0], \
            struct.unpack_from("<I", cg, 20)[0]
        if magic != 0x090255:
            say(f"cg {c}: bad magic")
            continue
        cs = struct.unpack_from("<4i", cg, 24)
        frsum = struct.unpack_from("<8I", cg, 52)
        iusedoff, freeoff, _, csumoff, clusteroff, nclusterblks = struct.unpack_from("<6I", cg, 92)
        initediblk = struct.unpack_from("<I", cg, 120)[0]
        bit = lambda off, i: (cg[off + i // 8] >> (i % 8)) & 1

        # Inodes
        nifree, ndir = ipg, 0
        for i in range(ipg):
            ino = c * ipg + i
            want = 1 if (ino in alloc_inodes or ino < 2) else 0
            if want:
                nifree -= 1
                ndir += ino in dirs
            if bit(iusedoff, i) != want:
                say(f"cg {c}: inode {ino} is {'used' if bit(iusedoff, i) else 'free'} in the map, "
                    f"but {'allocated' if want else 'unallocated'}")
            if want and i >= initediblk and ino >= 2:
                say(f"cg {c}: inode {ino} is past cg_initediblk {initediblk}")

        # Fragments
        # Each group's metadata (backup superblock, group block, inodes) runs
        # from sblkno to dblkno; in group 0 everything before it is the boot
        # area, in the others it is ordinary data space.
        meta_lo = c * fpg + (0 if c == 0 else sb(8))
        meta_hi = c * fpg + sb(20)
        nbfree = nffree = 0
        newfrsum = [0] * 8
        free_blocks = []
        for b in range(0, ndblk - ndblk % frag, frag):
            nfree = 0
            pattern = []
            for j in range(frag):
                f = c * fpg + b + j
                want_free = f not in used and not (meta_lo <= f < meta_hi) and f < fs_size
                if bit(freeoff, b + j) != want_free:
                    owner = used.get(f)
                    say(f"cg {c}: frag {f} is {'free' if bit(freeoff, b + j) else 'allocated'} in the map, "
                        f"but {'used by inode %d' % owner if owner is not None else 'unused'}")
                fr = bit(freeoff, b + j)
                nfree += fr
                pattern.append(fr)
            if nfree == frag:
                nbfree += 1
                free_blocks.append(1)
            else:
                free_blocks.append(0)
                if nfree:
                    nffree += nfree
                    run = 0
                    for fr in pattern + [0]:
                        if fr:
                            run += 1
                        else:
                            if run:
                                newfrsum[run] += 1
                            run = 0
        if (ndir, nbfree, nifree, nffree) != cs:
            say(f"cg {c}: cg_cs (ndir, nbfree, nifree, nffree) = {cs}, from the maps {(ndir, nbfree, nifree, nffree)}")
        if tuple(newfrsum) != frsum:
            say(f"cg {c}: cg_frsum {frsum}, from the map {tuple(newfrsum)}")
        if contig > 0:
            for i in range(nclusterblks):
                if bit(clusteroff, i) != free_blocks[i]:
                    say(f"cg {c}: cluster bit {i} is {bit(clusteroff, i)}, block free = {free_blocks[i]}")
            sums_cl = [0] * (contig + 1)
            run = 0
            for i in range(nclusterblks):
                if bit(clusteroff, i):
                    run += 1
                elif run:
                    sums_cl[min(run, contig)] += 1
                    run = 0
            if run:
                sums_cl[min(run, contig)] += 1
            # Slot 0 is unused, and overlaps the end of the fragment map.
            disk = [0] + list(struct.unpack_from(f"<{contig}I", cg, csumoff + 4))
            if disk != sums_cl:
                say(f"cg {c}: cluster summary {disk}, from the map {sums_cl}")
        arr = struct.unpack_from("<4i", img, csaddr * fsize + 16 * c)
        if arr != cs:
            say(f"cg {c}: fs_cs[{c}] = {arr}, cg_cs = {cs}")
        for k in range(4):
            sums[k] += arr[k]
    if tuple(sums) != total[:4]:
        say(f"fs_cstotal (ndir, nbfree, nifree, nffree) = {total[:4]}, sum of fs_cs = {tuple(sums)}")
    if total[4]:
        say(f"fs_cstotal.cs_numclusters = {total[4]} (fsck expects 0)")
    print(f"{len(alloc_inodes)} inodes, {len(used)} fragments in use; {problems} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "ufs.img"))
