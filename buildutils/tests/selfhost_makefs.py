# This file has been generated in whole or in part by a Large Language Model
"""Build the image tool against musl, then verify real UFS2 and ISO output."""
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'build/selfhost'
with tempfile.TemporaryDirectory(prefix='kunix-static-makefs-') as work:
    work = Path(work)
    binary = work / 'makefs'
    subprocess.run(['python3', str(ROOT / 'buildutils/selfhost-makefs.py'),
                    '--source', str(BASE / 'makefs-src'), '--cc', str(BASE / 'toolchain/bin/gcc'),
                    '--output', str(binary)], check=True)
    (work / 'root').mkdir()
    (work / 'root/hello').write_text('built with static musl makefs\n')
    subprocess.run([str(binary), '-t', 'ffs', '-o', 'version=2', '-s', '16m', str(work / 'root.img'), str(work / 'root')], check=True)
    with (work / 'root.img').open('rb') as image:
        found = False
        for block in (65536, 8192, 0, 262144):
            image.seek(block + 1372)
            found |= struct.unpack('<I', image.read(4))[0] == 0x19540119
        assert found
    subprocess.run([str(binary), '-t', 'cd9660', '-o', 'rockridge', str(work / 'root.iso'), str(work / 'root')], check=True)
    with (work / 'root.iso').open('rb') as image:
        image.seek(32769)
        assert image.read(5) == b'CD001'
    headers = subprocess.check_output(['readelf', '-h', '-l', '-d', str(binary)], text=True)
    assert 'EXEC (Executable file)' in headers
    assert 'INTERP' not in headers and '(NEEDED)' not in headers
print('static makefs: UFS2, ISO9660 and static ELF checks passed')
