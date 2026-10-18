#!/usr/bin/env python3
# This file has been generated in whole or in part by a Large Language Model
"""Compile MirBSD makefs with musl, using its existing GNU portability code."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--cc', required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
src = args.source.resolve() / 'src'
cur = src / 'usr.sbin/makefs'
nb = cur / 'nbsrc'
compat = Path(__file__).resolve().parent / 'selfhost-makefs-compat'
includes = [compat, cur, nb / 'sbin/mknod', nb / 'usr.sbin/mtree', nb / 'sys',
            src / 'sbin/mknod', src / 'usr.sbin/mtree', src / 'sys',
            src / 'sys/isofs/cd9660', nb / 'lib/libc/gen',
            nb / 'lib/libc/stdlib', src / 'include', nb / 'sys/fs/cd9660']
# This is the upstream Makefile's source list. Prefer its NetBSD compatibility
# files where specified, and the MirBSD implementations for filesystem tables.
names = '''cd9660.c ffs.c getid.c makefs.c misc.c pack_dev.c spec.c walk.c
ffs_alloc.c ffs_balloc.c ffs_bswap.c ffs_subr.c ffs_tables.c ufs_bmap.c buf.c mkfs.c
cd9660_strings.c cd9660_debug.c cd9660_eltorito.c cd9660_write.c cd9660_conversion.c
iso9660_rrip.c cd9660_archimedes.c pwcache.c strsuftoll.c fparseln.c setmode.c
unvis.c fgetln.c strlfun.c'''.split()
directories = [cur, cur / 'ffs', nb / 'sys/ufs/ffs', src / 'sys/ufs/ffs',
               cur / 'cd9660', nb / 'sys/fs/cd9660', nb / 'sbin/mknod',
               nb / 'usr.sbin/mtree', nb / 'lib/libc/gen', nb / 'lib/libc/stdlib',
               src / 'lib/libutil', src / 'lib/libc/gen',
               args.source.resolve() / 'contrib/code/mirmake/dist/contrib', src / 'kern/c']
files = []
for name in names:
    matches = [directory / name for directory in directories if (directory / name).is_file()]
    if not matches:
        raise RuntimeError('missing upstream makefs source: ' + name)
    files.append(matches[0])
defines = ['_GNU_SOURCE', 'GNUPORT', 'LIBC_SCCS', 'OUTSIDE_OF_LIBKERN',
           'NEED_STRLFUN_PROTOS', 'L_strlcpy', 'L_strlcat', 'S_ISTXT=S_ISVTX',
           'HAVE_STRUCT_STAT_BIRTHTIME=0', 'HAVE_NBTOOL_CONFIG_H=0',
           'HAVE_NETDB_H=1', 'HAVE_PWCACHE_USERDB=0', 'HAVE_STRSUFTOLL=0',
           'HAVE_STRUCT_STATVFS_F_IOSIZE=0', 'HAVE_STRUCT_STAT_ST_MTIMENSEC=0',
           'HAVE_STRUCT_STAT_ST_FLAGS=0', 'HAVE_STRUCT_STAT_ST_GEN=0',
           'bswap16=bswap_16', 'bswap32=bswap_32', 'bswap64=bswap_64']
subprocess.run([args.cc, '-static', '-no-pie', '-O2', '-include', str(compat / 'sys/cdefs.h'),
                *['-I' + str(directory) for directory in includes],
                *['-D' + define for define in defines], *map(str, files),
                '-o', str(args.output.resolve())], check=True)
