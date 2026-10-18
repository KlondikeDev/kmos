# This file has been generated in whole or in part by a Large Language Model
"""Build real image fixtures and check which native programs were installed."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / 'build/selfhost/toolchain'
SCRIPT = ROOT / 'buildutils/selfhost-image-guest.sh'


class ImageContents(unittest.TestCase):
    def test_new_native_binaries_replace_seed_without_shadowing_busybox(self):
        self.check_image()

    @unittest.skipUnless(Path('/dev/shm').is_dir(), 'requires a second filesystem')
    def test_seed_on_another_filesystem(self):
        with tempfile.TemporaryDirectory(prefix='kunix-image-seed-', dir='/dev/shm') as seed:
            self.check_image(seed)

    def check_image(self, external_seed=None):
        with tempfile.TemporaryDirectory(prefix='kunix-image-content-') as temporary:
            work = Path(temporary)
            if external_seed:
                if work.stat().st_dev == Path(external_seed).stat().st_dev:
                    self.skipTest('fixture directories share a filesystem')
                (work / 'seed').symlink_to(external_seed, target_is_directory=True)
            for directory in ('seed/bin', 'etc', 'build/user', 'build/pm', 'build/habilis',
                              'build/binaries', 'compat/primemeridian/tests', 'limine'):
                (work / directory).mkdir(parents=True)
            for name, data in (('busybox', b'busybox seed'), ('native', b'old native'),
                               ('native.elf', b'old native'), ('ls.elf', b'old native ls'), ('ported', b'ported command'),
                               ('ported.elf', b'old native ported')):
                (work / 'seed/bin' / name).write_bytes(data)
            os.link(work / 'seed/bin/busybox', work / 'seed/bin/ls')
            (work / 'seed/bin/ported-link').symlink_to('ported')
            (work / 'build/user/native.elf').write_bytes(b'fresh native from compiler')
            (work / 'build/user/ls.elf').write_bytes(b'fresh native ls from compiler')
            (work / 'build/user/ported.elf').write_bytes(b'fresh native ported')
            (work / 'build/habilis/habilis').write_bytes(b'new init fixture')
            (work / 'etc/passwd').write_text('root:x:0:0::/home/root:/bin/sh\n')
            (work / 'compat/primemeridian/tests/fixture.zsh').write_text('#!/bin/sh\n')
            shutil.copy2(ROOT / 'build/binaries/kunix-kodiak.elf', work / 'build/binaries')
            for name in ('limine-bios.sys', 'limine-bios-cd.bin'):
                shutil.copy2(ROOT / 'limine' / name, work / 'limine' / name)
            # A legitimate /bin/sh seed keeps the production script's seed
            # guard strict while avoiding a dependency on the host /bin tree.
            shutil.copy2('/bin/sh', work / 'seed/bin/sh')
            env = dict(os.environ, SELFHOST_PREFIX=str(TOOLS), SELFHOST_SEED_ROOT=str(work / 'seed'))
            result = subprocess.run(['sh', str(SCRIPT)], cwd=work, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            installed = work / 'build/selfhost-image/root/bin'
            self.assertEqual((installed / 'native.elf').read_bytes(), b'fresh native from compiler')
            self.assertEqual((installed / 'native').read_bytes(), b'fresh native from compiler')
            self.assertEqual((installed / 'ls.elf').read_bytes(), b'fresh native ls from compiler')
            self.assertEqual((installed / 'ls').read_bytes(), b'busybox seed')
            self.assertTrue(os.path.samefile(installed / 'ls', installed / 'busybox'))
            self.assertEqual((installed / 'ported').read_bytes(), b'ported command')
            self.assertEqual((installed / 'ported.elf').read_bytes(), b'fresh native ported')
            self.assertEqual((installed / 'ported-link').readlink(), Path('ported'))
            self.assertEqual((work / 'seed/bin/native.elf').read_bytes(), b'old native')
            self.assertEqual((work / 'seed/bin/native').read_bytes(), b'old native')
            self.assertIn(b'fresh native from compiler', (work / 'build/selfhost-image/iso/boot/root.img').read_bytes())
            # A repeated image build must not retain programs removed from
            # the current build, even if the seed still has its old aliases.
            (work / 'build/user/native.elf').unlink()
            result = subprocess.run(['sh', str(SCRIPT)], cwd=work, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse((installed / 'native.elf').exists())
            self.assertFalse((installed / 'native').exists())


if __name__ == '__main__':
    unittest.main()
