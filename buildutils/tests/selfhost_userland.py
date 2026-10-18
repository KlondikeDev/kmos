# This file has been generated in whole or in part by a Large Language Model
"""Compile a minimal real userland and reject obsolete generated outputs."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='kunix-userland-refresh-') as temporary:
    root = Path(temporary)
    subprocess.run(['python3', str(ROOT / 'buildutils/selfhost.py'), 'stage', str(root)],
                   check=True, capture_output=True)
    src = root / 'usr/src/kunix'
    for directory in ('user', 'compat/primemeridian/tests'):
        shutil.rmtree(src / directory)
        (src / directory).mkdir(parents=True)
    (src / 'user/lib').mkdir()
    for name in ('crt0.s', 'user.ld'):
        shutil.copy2(ROOT / 'user' / name, src / 'user' / name)
    (src / 'user/lib/libk.c').write_text('void __libk_start(void) {}\n')
    for name in ('user/current.c', 'compat/primemeridian/tests/current.c',
                 'habilis/habilis.c', 'network/netconfig.c', 'system/update.c'):
        (src / name).write_text('int main(void) { return 0; }\n')
    for name in ('build/user/deleted.elf', 'build/pm/deleted', 'build/habilis/deleted'):
        path = src / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b'obsolete compiled program')
    env = dict(os.environ, SELFHOST_PREFIX=str(root / 'opt/selfhost'))
    result = subprocess.run(['sh', 'buildutils/selfhost-build.sh', 'userland'], cwd=src,
                            env=env, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    for name in ('build/user/deleted.elf', 'build/pm/deleted', 'build/habilis/deleted'):
        assert not (src / name).exists(), 'obsolete output retained: ' + name
    for name in ('build/user/current.elf', 'build/pm/current', 'build/habilis/habilis'):
        assert (src / name).read_bytes()[:4] == b'\x7fELF', name
print('selfhost userland: current ELF outputs built, obsolete outputs discarded')
