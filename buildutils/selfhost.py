#!/usr/bin/env python3
# This file has been generated in whole or in part by a Large Language Model
"""Prepare the source tree and static development tools for a Kunix root image."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def source(destination):
    destination.mkdir(parents=True, exist_ok=True)
    # Generated objects and host image files never enter the source bundle.
    for name in ('arch', 'boot', 'compat', 'drivers', 'fs', 'habilis', 'include', 'kernel', 'lib', 'user', 'vendor', 'network', 'system', 'etc', 'limine'):
        shutil.copytree(ROOT / name, destination / name, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('*.o', '__pycache__', '.git', '*.pyc'))
    (destination / 'buildutils').mkdir(exist_ok=True)
    for name in ('selfhost-build.sh', 'selfhost-image-guest.sh', 'selfhost.mk', 'linker.ld'):
        shutil.copy2(ROOT / 'buildutils' / name, destination / 'buildutils' / name)
    for name in ('LICENSE', 'LARGELANGUAGEMODELS.TXT'):
        shutil.copy2(ROOT / name, destination / name)
    build = (ROOT / 'buildutils/build.sh').read_text()
    functions = []
    for name in ('build', 'link'):
        match = re.search(r'^' + name + r'\(\)\{\n(.*?)^\}', build, re.M | re.S)
        if match is None:
            raise RuntimeError('cannot locate build.sh ' + name + ' function')
        body = match.group(0)
        body = re.sub(r'^.*python3 buildutils/build-id.py.*\n', '', body, flags=re.M)
        functions.append(body)
    # The command list comes from the same build.sh as the host build, including
    # its upstream source loops. The guest only replaces the compiler driver.
    (destination / 'buildutils/selfhost-commands.sh').write_text(
        '# This file has been generated in whole or in part by a Large Language Model\n'
        + '\n'.join(functions) + '\n')
    (destination / 'build/generated').mkdir(parents=True, exist_ok=True)
    header = subprocess.check_output(['python3', str(ROOT / 'buildutils/build-id.py')], cwd=ROOT)
    (destination / 'build/generated/kunix_build.h').write_bytes(header)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    entries = ['This file has been generated in whole or in part by a Large Language Model',
               'Source snapshot of Kunix revision ' + revision,
               'Includes working tree changes. Build ID header frozen at bundle creation.',
               'SHA256 file manifest: SOURCE-SHA256SUMS']
    (destination / 'SOURCE-PROVENANCE').write_text('\n'.join(entries) + '\n')
    manifest = []
    for path in sorted(destination.rglob('*')):
        if path.is_file() and path.name != 'SOURCE-SHA256SUMS':
            manifest.append(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + str(path.relative_to(destination)))
    (destination / 'SOURCE-SHA256SUMS').write_text('\n'.join(manifest) + '\n')


def stage(destination, toolchain):
    if not (toolchain / 'bin/gcc').is_file() or not (toolchain / 'bin/make').is_file():
        raise RuntimeError('run buildutils/selfhost-toolchain.sh first (static GCC and make required)')
    source(destination / 'usr/src/kunix')
    target = destination / 'opt/selfhost'
    if target.exists():
        shutil.rmtree(target)
    linked = {}
    def copy_tool(src, dst):
        stat = os.stat(src)
        key = (stat.st_dev, stat.st_ino)
        if key in linked:
            Path(dst).unlink(missing_ok=True)
            os.link(linked[key], dst)
        else:
            shutil.copy2(src, dst)
            linked[key] = dst
        return dst
    shutil.copytree(toolchain, target, dirs_exist_ok=True, symlinks=True, copy_function=copy_tool,
                    ignore=shutil.ignore_patterns('*.so', '*.so.*'))
    # Prime Meridian has no dynamic loader. Check every ELF, including GCC's
    # subprocesses; a static gcc driver with a dynamic cc1 is not useful.
    checked = set()
    for path in target.rglob('*'):
        if path.is_symlink() or not path.is_file() or path.read_bytes()[:4] != b'\x7fELF':
            continue
        if int.from_bytes(path.read_bytes()[16:18], 'little') == 3:
            raise RuntimeError('static PIE requires unsupported ET_DYN loading: ' + str(path))
        digest = hashlib.sha256(path.read_bytes()).digest()
        if digest in checked:
            continue
        checked.add(digest)
        headers = subprocess.check_output(['readelf', '-l', '-d', str(path)], text=True)
        if 'INTERP' in headers or '(NEEDED)' in headers:
            raise RuntimeError('development tool requires dynamic loading: ' + str(path))
    (destination / 'bin').mkdir(exist_ok=True)
    shutil.copy2(ROOT / 'buildutils/selfhost-test.sh', destination / 'bin/selfhost-test')
    for name in ('gcc', 'cc', 'as', 'ld', 'ar', 'ranlib', 'nm', 'strip', 'objcopy', 'objdump', 'readelf', 'make'):
        path = destination / 'bin' / name
        if not path.exists() and not path.is_symlink():
            path.symlink_to('/opt/selfhost/bin/' + ('gcc' if name == 'cc' else name))
    wrappers = destination / 'opt/selfhost/kernel-bin'
    wrappers.mkdir(exist_ok=True)
    for name, program in (('gcc', 'gcc -fno-pie -no-pie -fno-asynchronous-unwind-tables -mstack-protector-guard=global'), ('as', 'as')):
        path = wrappers / ('x86_64-elf-' + name)
        path.write_text('#!/bin/sh\n# This file has been generated in whole or in part by a Large Language Model\nexec "${0%/*}/../bin/' + program.split()[0] + '" ' + ' '.join(program.split()[1:]) + ' "$@"\n')
        path.chmod(0o755)
    print('Self-host tools and sources staged: ' + str(destination))


def kit(destination, toolchain):
    """A relocatable archive intended for persistent /home storage."""
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='kunix-selfhost-kit-') as work:
        stage(Path(work), toolchain)
        with tarfile.open(destination, 'w:gz') as archive:
            archive.add(Path(work) / 'opt/selfhost', arcname='selfhost/tools')
            archive.add(Path(work) / 'usr/src/kunix', arcname='selfhost/src')
    print('Persistent development kit: ' + str(destination))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('source', 'stage', 'kit'))
    parser.add_argument('destination', type=Path)
    parser.add_argument('--toolchain', type=Path, default=ROOT / 'build/selfhost/toolchain')
    args = parser.parse_args()
    if args.action == 'source':
        source(args.destination)
    elif args.action == 'kit':
        kit(args.destination, args.toolchain)
    else:
        stage(args.destination, args.toolchain)

if __name__ == '__main__':
    main()
