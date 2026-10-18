#!/usr/bin/env python3
# This file has been generated in whole or in part by a Large Language Model
"""Compile Kunix and an ISO inside QEMU, export it to scratch NVMe, boot it."""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--iso', type=Path, default=ROOT / 'build/selfhost/full-live.iso')
parser.add_argument('--timeout', type=int, default=3600)
args = parser.parse_args()
base = ROOT / 'build/selfhost'
base.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='export-', dir=base) as work:
    scratch = Path(work) / 'nvme.img'
    with scratch.open('wb') as disk:
        disk.truncate(256 * 1024 * 1024)
    command = [sys.executable, str(ROOT / 'buildutils/test.py'), '--iso', str(args.iso),
               '--no-ata', '--nvme', str(scratch), '--mem', '2560M',
               '--timeout', str(args.timeout), '--command', 'selfhost-test image',
               '--log', str(base / 'guest-full.log'), '--serial-live', str(base / 'serial-full.log')]
    subprocess.run(command, cwd=ROOT, check=True)
    log = (base / 'guest-full.log').read_text()
    if 'SELFHOST KERNEL PASS' not in log or 'SELFHOST USERLAND PASS' not in log or 'SELFHOST IMAGE PASS' not in log:
        raise RuntimeError('guest compilation/image evidence is incomplete')
    with scratch.open('rb') as disk:
        disk.seek(16 * 2048)
        pvd = disk.read(2048)
        if pvd[1:6] != b'CD001':
            raise RuntimeError('scratch NVMe does not contain the guest ISO')
        volume = struct.unpack_from('<I', pvd, 80)[0] * 2048
        # The ISO file may carry trailing padding past the volume size, so
        # the guest reports its exact byte count; it must cover the volume.
        reported = re.search(r'^ISO_BYTES (\d+)$', log, re.M)
        size = int(reported.group(1)) if reported else 0
        if size < volume or size > scratch.stat().st_size:
            raise RuntimeError('invalid guest ISO size')
        disk.seek(0)
        data = disk.read(size)
    expected = re.search(r'([0-9a-f]{64})\s+/var/tmp/kunix-selfhost.iso', log)
    digest = hashlib.sha256(data).hexdigest()
    if expected is None or expected.group(1) != digest:
        raise RuntimeError('exported ISO SHA256 differs from the guest output')
    output = base / 'kunix-guest-built.iso'
    output.write_bytes(data)
    print(f'Guest-built ISO SHA256: {digest}', flush=True)
    # The file-backed suites need the FAT disk (ktest mmaps /fat, pmsmoke
    # writes it). Boot with the stock ATA disks attached; the RAM root still
    # comes from the exported ISO.
    for suite in ('ktest', 'pmtest', 'pmsmoke', 'permtest', 'fbtest', 'xtest', 'vmtest'):
        subprocess.run([sys.executable, str(ROOT / 'buildutils/test.py'), '--iso', str(output),
                        '--mem', '384M', '--timeout', '240', '--command', suite,
                        '--log', str(base / ('guest-built-' + suite + '.log'))], cwd=ROOT, check=True)
print('selfhost: guest kernel, userland, ISO export, SHA256 and boot suites passed')
