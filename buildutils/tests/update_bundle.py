# This file has been generated in whole or in part by a Large Language Model
import hashlib
from pathlib import Path
import subprocess
import tempfile
import struct

ROOT=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as d:
    d=Path(d)
    (d/'kernel').write_bytes(b'\x7fELF'+b'K'*4092)
    with (d/'root').open('wb') as f:
        f.truncate(128*1024*1024)
    subprocess.run(['python3',str(ROOT/'buildutils/update-bundle.py'),str(d/'kernel'),str(d/'root'),str(d/'update.pkg')],check=True)
    h=(d/'update.pkg').read_bytes()[:512]
    assert h[:8]==b'KXUPD001'
    assert struct.unpack_from('<QQ',h,8)==(4096,128*1024*1024)
    assert h[24:56]==hashlib.sha256((d/'kernel').read_bytes()).digest()
    assert h[56:88]==hashlib.sha256((d/'root').read_bytes()).digest()
    assert (d/'update.pkg').stat().st_size==512+4096+128*1024*1024
print('update bundle: exact lengths and independent SHA-256 passed')
