#!/usr/bin/env python3
# This file has been generated in whole or in part by a Large Language Model
"""Exercise real EFI updates across disposable NVMe boots; never uses host devices.

Requires a completed normal image build. Every run creates a new image under
build/phase5; previous test images and installed development disks are untouched.
"""
import argparse
from contextlib import ExitStack
import functools
import hashlib
import http.server
from pathlib import Path
import struct
import shutil
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent


class HTTP(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_args):
        pass


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--block-size', type=int, choices=(512, 4096), default=512)
    ap.add_argument('--iso', type=Path, default=ROOT/'build/binaries/kunix-live.iso')
    ap.add_argument('--root-image', type=Path, default=ROOT/'ufs.img')
    ap.add_argument('--kernel', type=Path, default=ROOT/'build/binaries/kunix-kodiak.elf')
    ap.add_argument('--bundle', type=Path, help='existing bundle, otherwise package the current kernel and ufs.img')
    args = ap.parse_args()
    base = ROOT/'build/phase5'
    base.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=f'update-{args.block_size}-', dir=base))
    served = work/'http'
    served.mkdir()
    image = work/'nvme.img'
    assert image.resolve().is_relative_to(base.resolve())
    with image.open('xb') as f:
        f.truncate(2*1024**3)
    bundle = served/'update.pkg'
    if args.bundle:
        shutil.copyfile(args.bundle, bundle)
    else:
        subprocess.run([sys.executable, str(ROOT/'buildutils/update-bundle.py'),
                        str(args.kernel), str(args.root_image), str(bundle)], check=True)
    with bundle.open('rb') as source:
        header = source.read(512)
        assert header[:8] == b'KXUPD001'
        ksize, rsize = struct.unpack_from('<QQ', header, 8)
        kernel = source.read(ksize)
        assert len(kernel) == ksize and rsize == 128*1024**2
        # ELF loaders ignore trailing bytes. A distinct harmless trailer makes
        # slot B's kernel hash differ, proving this really stages a new payload.
        (work/'second-kernel.elf').write_bytes(kernel+b'\nKunix update integration generation B\n')
        root_image = work/'base-root.img'
        with root_image.open('wb') as out:
            while chunk := source.read(1024*1024):
                out.write(chunk)
    subprocess.run([sys.executable, str(ROOT/'buildutils/update-bundle.py'),
                    str(work/'second-kernel.elf'), str(root_image), str(served/'second.pkg')], check=True)
    (served/'malformed.pkg').write_bytes(b'invalid update fixture\n')
    (served/'incomplete.pkg').write_bytes(header)

    def run(label, command, live=False, answer=None):
        cmd = [sys.executable, str(ROOT/'buildutils/test.py'), '--no-ata', '--machine', 'q35',
               '--mem', '512M', '--network', '--nvme', str(image),
               '--nvme-block-size', str(args.block_size), '--timeout', '900',
               '--command', command, '--log', str(work/f'{label}-serial.log')]
        if live:
            cmd += ['--iso', str(args.iso)]
        else:
            cmd += ['--boot-nvme', '--firmware', '/usr/share/OVMF/OVMF_CODE_4M.fd',
                    '--firmware-vars', '/usr/share/OVMF/OVMF_VARS_4M.fd']
        if answer:
            cmd += ['--install-answer', answer]
        with (work/f'{label}.log').open('w') as log:
            result = subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        print(f'{label}:\n{(work/f"{label}.log").read_text()}', flush=True)
        if result.returncode:
            raise RuntimeError(f'{label} failed; see {work}')

    def verify_slot(slot, expected_bundle):
        with image.open('rb') as disk:
            disk.seek(2*args.block_size)
            entry = disk.read(128)
        first = struct.unpack_from('<Q', entry, 32)[0]
        spec = f'{image}@@{first*args.block_size}'
        config = subprocess.check_output(['mtype', '-i', spec, '::limine.conf'])
        selected = f'kernel_path: boot():/KUPDATE{slot}/KERNEL.ELF'.encode()
        assert selected in config and config.index(selected) < config.index(b'original installation')
        assert b'kernel_path: boot():/KUNIX.ELF' in config
        assert b'root=PARTUUID=' in config and b'home=PARTUUID=' in config
        expected = expected_bundle.open('rb')
        with expected:
            h = expected.read(512)
        for name, size, digest in [('KERNEL.ELF', struct.unpack_from('<Q', h, 8)[0], h[24:56]),
                                    ('root.img', 128*1024**2, h[56:88])]:
            target = work/f'{slot}-{name}'
            subprocess.run(['mcopy', '-o', '-i', spec, f'::/KUPDATE{slot}/{name}', str(target)], check=True)
            actual = hashlib.sha256()
            with target.open('rb') as payload:
                remaining = size
                while remaining:
                    chunk = payload.read(min(1024*1024, remaining))
                    assert chunk
                    actual.update(chunk)
                    remaining -= len(chunk)
            assert actual.digest() == digest, f'{slot} {name} readback hash mismatch'
        ready = subprocess.check_output(['mtype', '-i', spec, f'::/KUPDATE{slot}/READY.BIN'])
        assert ready[:512] == h
        print(f'PASS host readback: slot {slot} payload hashes, ready record and original boot entry', flush=True)

    with ExitStack() as stack:
        server = http.server.ThreadingHTTPServer(('127.0.0.1', 18084),
                    functools.partial(HTTP, directory=str(served)))
        stack.callback(server.server_close)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        stack.callback(server.shutdown)
        run('install', 'install-kunix /dev/disk8', live=True, answer='YES')
        run('seed-installed-data', 'storagetest persist')
        run('enroll-A', 'updatetest enroll')
        verify_slot('A', bundle)
        run('boot-A-stage-B', 'updatetest stage')
        verify_slot('B', served/'second.pkg')
        run('boot-B-rollback-A', 'updatetest rollback')
        verify_slot('A', bundle)
        run('boot-rollback-A', 'updatetest verify')
    print(f'PASS real NVMe {args.block_size} update enrollment, A/B boots, rollback and retained data; logs: {work}', flush=True)


if __name__ == '__main__':
    main()
