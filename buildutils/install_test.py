# This file has been generated in whole or in part by a Large Language Model
"""Install and boot a disposable NVMe VM image. Never accepts host devices.

python3 buildutils/install_test.py --block-size 4096 --reset
Only files under build/phase3 are allowed; --reset deliberately recreates them.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / 'build/phase3'


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(8 * 1024 * 1024), b''):
            h.update(chunk)
    return h.digest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--block-size', type=int, choices=(512, 4096), default=512)
    ap.add_argument('--reset', action='store_true', help='recreate this disposable VM disk')
    args = ap.parse_args()
    work = WORK / f'integration-{args.block_size}'
    work.mkdir(parents=True, exist_ok=True)
    image = work / 'nvme.img'
    if not image.resolve().is_relative_to(WORK.resolve()):
        sys.exit('test disk must be inside build/phase3')
    if not args.reset:
        sys.exit('--reset is required to create the disposable installation image')
    with image.open('wb') as f:
        f.truncate(2 * 1024**3)

    def run(label, command, live=True, answer=None, refusal=None, power=False):
        log = work / f'{label}.log'
        cmd = [sys.executable, str(ROOT/'buildutils/test.py'), '--no-ata', '--machine', 'q35',
               '--mem', '512M', '--nvme', str(image), '--nvme-block-size', str(args.block_size),
               '--timeout', '240', '--command', command, '--log', str(work/f'{label}-serial.log')]
        if live:
            cmd += ['--iso', str(ROOT/'build/binaries/kunix-live.iso')]
        else:
            cmd += ['--boot-nvme', '--firmware', '/usr/share/OVMF/OVMF_CODE_4M.fd',
                    '--firmware-vars', '/usr/share/OVMF/OVMF_VARS_4M.fd']
        if answer:
            cmd += ['--install-answer', answer]
        if refusal:
            cmd += ['--expect-boot-error', refusal]
        if power:
            cmd += ['--expect-power', 'off']
        with log.open('w') as output:
            result = subprocess.run(cmd, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT)
        text = log.read_text()
        print(f'{label}:\n{text}', flush=True)
        if result.returncode:
            raise RuntimeError(f'{label} failed; see {log}')

    run('storage', 'storagetest write', answer='YES')
    before = digest(image)
    run('plan', 'install-kunix /dev/disk8 --plan')
    assert digest(image) == before, 'plan wrote to disk'
    run('cancel', 'install-kunix /dev/disk8', answer='NO')
    assert digest(image) == before, 'canceled installation wrote to disk'
    run('install', 'install-kunix /dev/disk8', answer='YES')
    run('persist', 'storagetest persist', live=False)
    run('verify', 'storagetest verify', live=False)
    run('poweroff', 'poweroff', live=False, power=True)

    with image.open('rb') as f:
        f.seek(2 * args.block_size)
        entries = f.read(128*3)
    parts = []
    for i in range(3):
        first, last = struct.unpack_from('<QQ', entries, i*128+32)
        parts.append((first*args.block_size, (last-first+1)*args.block_size))
    spec = f'{image}@@{parts[0][0]}'
    config = subprocess.check_output(['mtype', '-i', spec, '::limine.conf'])

    def config_write(data):
        path = work/'limine.conf'
        path.write_bytes(data)
        subprocess.run(['mcopy', '-o', '-i', spec, str(path), '::limine.conf'], check=True)

    # Invalid and overlapping selectors must not mount another root as fallback.
    root_uuid = re.search(rb'root=PARTUUID=([^ \n]+)', config).group(1)
    try:
        config_write(re.sub(rb'home=PARTUUID=[^ \n]+', b'home=PARTUUID='+root_uuid, config))
        run('overlap-refused', '', live=False, refusal='boot: root/home devices overlap')
        config_write(re.sub(rb'root=PARTUUID=[^ \n]+', b'root=unsupported', config))
        run('selector-refused', '', live=False, refusal='no root filesystem: invalid PARTUUID selector')
    finally:
        config_write(config)

    # Reproduce the marker written before non-atomic ufsgrow metadata changes.
    root_at, root_size = parts[1]
    with image.open('r+b') as f:
        for location in (65536, 8192, 0, 262144):
            f.seek(root_at+location)
            sb = f.read(1376)
            if len(sb)==1376 and struct.unpack_from('<I', sb, 1372)[0]==0x19540119:
                break
        else:
            raise RuntimeError('installed UFS superblock missing')
        original = sb
        marked = bytearray(sb)
        marked[209] = 0
        struct.pack_into('<I', marked, 1312, struct.unpack_from('<I', sb, 1312)[0] | 4)
        f.seek(root_at+location)
        f.write(marked)
    try:
        run('needs-fsck-refused', '', live=False, refusal='requires fsck after incomplete metadata changes')
    finally:
        with image.open('r+b') as f:
            f.seek(root_at+location)
            f.write(original)

    # Sparse extraction keeps metadata checks off host physical devices.
    for index, (offset, count) in enumerate(parts):
        target = work/f'partition{index+1}.img'
        with image.open('rb') as src, target.open('wb') as dst:
            src.seek(offset)
            remaining = count
            while remaining:
                data = src.read(min(4*1024*1024, remaining))
                if not data:
                    raise RuntimeError('short partition extraction')
                if data.strip(b'\0'):
                    dst.write(data)
                else:
                    dst.seek(len(data), 1)
                remaining -= len(data)
            dst.truncate(count)
        command = ['/usr/sbin/fsck.fat', '-vn', str(target)] if index==0 else [str(Path.home()/'opt/kunix-host/bin/fsck_ffs'), '-fn', str(target)]
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (work/f'partition{index+1}-fsck.log').write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f'fsck failed for {target}:\n{result.stdout}')
    print(f'PASS full NVMe {args.block_size}-byte installation, cancellation, UEFI boot, persistence, refusal cases and fsck', flush=True)


if __name__ == '__main__':
    main()
