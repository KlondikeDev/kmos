#!/usr/bin/env bash
# This file has been generated in whole or in part by a Large Language Model

# Copyright (c) 2026 Joseph Jones
# Licensed under the Kunix License v1.0 or later
# See LICENSE for details

# Build FreeBSD's fsck_ffs for the host, to check the UFS2 images Kunix
# writes. Debian no longer packages ufsutils, so this takes its last source
# (FreeBSD 10.0's sbin/fsck_ffs and lib/libufs) from snapshot.debian.org and
# compiles it against glibc with the shims in buildutils/fsck_ffs-compat.
#
#   buildutils/fsck-ffs.sh                 -> ~/opt/kunix-host/bin/fsck_ffs
#   ~/opt/kunix-host/bin/fsck_ffs -n ufs.img

set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
PREFIX=${KUNIX_HOST_PREFIX:-$HOME/opt/kunix-host}
WORK=$PREFIX/src/ufsutils
SNAP=https://snapshot.debian.org/file
ORIG_SHA1=f4406fa624d84e0869715be4865ec117d487b654   # ufsutils_10.0.orig.tar.gz
DEB_SHA1=b5d9d838ca40bde0b9743a504bdf5cc6b46cdc13    # ufsutils_10.0-3.debian.tar.xz

mkdir -p "$WORK" "$PREFIX/bin"
cd "$WORK"
for pair in "$ORIG_SHA1 orig.tar.gz" "$DEB_SHA1 deb.tar.xz"; do
    set -- $pair
    [ -f "$2" ] || curl -fL --retry 3 -o "$2" "$SNAP/$1"
    echo "$1  $2" | sha1sum -c --quiet
done
rm -rf ufsutils-10.0 debian build
tar xzf orig.tar.gz
tar xJf deb.tar.xz
cd ufsutils-10.0
grep -v '^#' ../debian/patches/series | grep . | while read -r p; do patch -p1 -s < "../debian/patches/$p"; done
# libufs asks statfs(2) where a mounted directory's device is; glibc's
# statfs cannot say, and we only ever check image files anyway.
python3 - lib/libufs/type.c <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
a = """		if (statfs(name, &sfs) < 0) {
			ERROR(disk, "could not find special device");
			return (-1);
		}
		strlcpy(dev, sfs.f_mntfromname, sizeof(dev));
		name = dev;"""
assert a in s
s = s.replace(a, '\t\tERROR(disk, "could not find special device");\n\t\treturn (-1);')
s = s.replace("\tstruct statfs sfs;\n", "")
open(p, "w").write(s)
PY

mkdir -p ../build
cd ../build
COMPAT=$ROOT/buildutils/fsck_ffs-compat
U=../ufsutils-10.0
for f in $U/sbin/fsck_ffs/*.c $U/lib/libufs/*.c $U/sys/ufs/ffs/ffs_subr.c \
         $U/sys/ufs/ffs/ffs_tables.c $U/sbin/mount/getmntopts.c; do
    # Two inode.c's (fsck's and libufs's): prefix objects with their directory.
    obj=$(basename "$(dirname "$f")")_$(basename "$f" .c).o
    gcc -c -w -O1 -fcommon -D_GNU_SOURCE -D_LIBUFS -include "$COMPAT/compat.h" -I"$COMPAT" \
        -I$U/sys -I$U/lib/libufs -I$U/sbin/fsck_ffs -I$U/sbin/mount "$f" -o "$obj"
done
gcc -o "$PREFIX/bin/fsck_ffs" ./*.o
echo "built $PREFIX/bin/fsck_ffs"
