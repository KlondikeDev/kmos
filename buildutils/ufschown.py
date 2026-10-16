#!/usr/bin/env python3
# This file has been generated in whole or in part by a Large Language Model

# Copyright (c) 2026 Joseph Jones
# Licensed under the Kunix License v1.0 or later
# See LICENSE for details

"""Give every inode in a UFS2 image to one owner (root, by default).

makefs copies the building user's uid and gid into the image; on Kunix
everything there should belong to root.

    python3 buildutils/ufschown.py ufs.img [uid gid]
"""

import struct
import sys

SBLOCK_SEARCH = [65536, 8192, 0, 262144]


def main(path, uid=0, gid=0):
    with open(path, "r+b") as f:
        img = bytearray(f.read())
        for loc in SBLOCK_SEARCH:
            if struct.unpack_from("<i", img, loc + 1372)[0] == 0x19540119:
                break
        else:
            sys.exit(f"{path}: no UFS2 superblock")
        sb = lambda off, fmt="<i": struct.unpack_from(fmt, img, loc + off)[0]
        iblkno, ncg, fsize, frag = sb(16), sb(44, "<I"), sb(52), sb(56)
        ipg, fpg, inopb = sb(184, "<I"), sb(188), sb(120, "<I")
        changed = 0
        for ino in range(ncg * ipg):
            c, idx = divmod(ino, ipg)
            off = (c * fpg + iblkno + (idx // inopb) * frag) * fsize + (idx % inopb) * 256
            if struct.unpack_from("<H", img, off)[0] == 0:
                continue                         # unallocated
            struct.pack_into("<II", img, off + 4, uid, gid)
            changed += 1
        f.seek(0)
        f.write(img)
    return changed


if __name__ == "__main__":
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    main(args[0], *(int(a) for a in args[1:3]))
