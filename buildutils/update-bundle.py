#!/usr/bin/env python3
# This file has been generated in whole or in part by a Large Language Model
"""Create a checked Kunix base update. Transfer over authenticated SSH.

Usage: update-bundle.py KERNEL ROOT.IMG OUTPUT.pkg
This does not repartition disks or package home data and credentials.
"""
import hashlib
from pathlib import Path
import struct
import sys

def main():
    if len(sys.argv)!=4:
        sys.exit(__doc__)
    kernel,root,out=map(Path,sys.argv[1:])
    sizes=(kernel.stat().st_size,root.stat().st_size)
    if not 0<sizes[0]<=4*1024*1024 or sizes[1]!=128*1024*1024:
        sys.exit('kernel must be <=4 MiB; base root must be exactly128 MiB')
    with kernel.open('rb') as f:
        if f.read(4)!=b'\x7fELF':
            sys.exit('kernel is not ELF')
    header=bytearray(512)
    header[:8]=b'KXUPD001'
    struct.pack_into('<QQ',header,8,*sizes)
    for path,at in ((kernel,24),(root,56)):
        digest=hashlib.sha256()
        with path.open('rb') as f:
            for b in iter(lambda:f.read(1024*1024),b''):
                digest.update(b)
        header[at:at+32]=digest.digest()
    if out.resolve() in (kernel.resolve(),root.resolve()):
        sys.exit('output must differ from input')
    temporary=out.with_name(out.name+'.part')
    with temporary.open('xb') as f:
        f.write(header)
        for path in (kernel,root):
            with path.open('rb') as src:
                for b in iter(lambda:src.read(1024*1024),b''):
                    f.write(b)
    temporary.replace(out)
    print(f'{out}: kernel {sizes[0]} bytes, root {sizes[1]} bytes')
if __name__=='__main__':
    main()
