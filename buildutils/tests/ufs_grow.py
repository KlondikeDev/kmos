# This file has been generated in whole or in part by a Large Language Model
"""Offline grow integrity tests. Run: python3 buildutils/tests/ufs_grow.py."""
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'compat/primemeridian/tests/ufsgrow.c'
FSCK = Path.home() / 'opt/kunix-host/bin/fsck_ffs'


def run(*args):
    return subprocess.run([str(a) for a in args], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True)


def geometry(path):
    with open(path, 'rb') as f:
        for loc in (65536, 8192, 0, 262144):
            f.seek(loc)
            sb = f.read(1376)
            if len(sb) == 1376 and struct.unpack_from('<I', sb, 1372)[0] == 0x19540119:
                return loc, sb
    raise AssertionError('missing superblock')


def preserved(path):
    """Snapshot all old inode tables and inode-owned blocks, independently of maps."""
    _, sb = geometry(path)
    val = lambda off, fmt='i': struct.unpack_from('<' + fmt, sb, off)[0]
    ncg, bsize, fsize, ipg, fpg = val(44, 'I'), val(48), val(52), val(184, 'I'), val(188)
    saved = {}
    with open(path, 'rb') as f:
        def save(off, size):
            f.seek(off)
            data = f.read(size)
            saved[(off, size)] = hashlib.sha256(data).digest()
            return data
        def indirect(addr, level):
            ptrs = struct.unpack('<' + 'q' * (bsize // 8), save(addr * fsize, bsize))
            for p in ptrs:
                if p:
                    if level:
                        indirect(p, level - 1)
                    else:
                        save(p * fsize, bsize)
        for c in range(ncg):
            table = save((c * fpg + val(16)) * fsize, ipg * 256)
            for i in range(ipg):
                d = table[i * 256:(i + 1) * 256]
                mode = struct.unpack_from('<H', d)[0]
                size, blocks = struct.unpack_from('<QQ', d, 16)
                if not mode or (mode & 0o170000) in (0o020000, 0o060000) or not blocks:
                    continue
                for j, p in enumerate(struct.unpack_from('<12q', d, 112)):
                    if p:
                        amount = min(bsize, max(0, size - j * bsize))
                        save(p * fsize, ((amount + fsize - 1) // fsize) * fsize)
                for level, p in enumerate(struct.unpack_from('<3q', d, 208)):
                    if p:
                        indirect(p, level)
    return saved


class Grow(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='kunix-ufsgrow-')
        cls.work = Path(cls.temp.name)
        cls.binary = cls.work / 'ufsgrow'
        assert SOURCE.exists(), 'offline ufsgrow implementation is missing'
        result = run('gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2', SOURCE, '-o', cls.binary)
        assert result.returncode == 0, result.stdout
        cls.tree = cls.work / 'tree'
        cls.tree.mkdir()
        (cls.tree / 'nested').mkdir()
        (cls.tree / 'nested/payload').write_bytes(bytes(range(256)) * 2100)
        (cls.tree / 'tail').write_bytes(b'fragment tail\n' * 121)
        (cls.tree / 'link').symlink_to('nested/payload')
        cls.template = cls.work / 'root.img'
        result = run('makefs', '-t', 'ffs', '-o', 'version=2', '-s', '128m', '-f', '8192', cls.template, cls.tree)
        assert result.returncode == 0, result.stdout

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def grow_check(self, source, target, diag=True):
        image = self.work / 'target.img'
        shutil.copyfile(source, image)
        snapshot = preserved(image)
        with open(image, 'r+b') as f:
            f.truncate(target)
        result = run(self.binary, image, target)
        self.assertEqual(result.returncode, 0, result.stdout)
        _, sb = geometry(image)
        self.assertEqual(struct.unpack_from('<q', sb, 1080)[0] * struct.unpack_from('<i', sb, 52)[0], target)
        with open(image, 'rb') as f:
            fsize = struct.unpack_from('<i', sb, 52)[0]
            fpg = struct.unpack_from('<i', sb, 188)[0]
            sblk = struct.unpack_from('<i', sb, 8)[0]
            for c in range(struct.unpack_from('<I', sb, 44)[0]):
                f.seek((c * fpg + sblk) * fsize)
                self.assertEqual(f.read(1376), sb, f'stale backup in cylinder group {c}')
            for (off, size), digest in snapshot.items():
                f.seek(off)
                self.assertEqual(hashlib.sha256(f.read(size)).digest(), digest, f'old data changed at {off}')
        result = run(FSCK, '-fn', image)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertNotIn('INCORRECT', result.stdout)
        self.assertNotIn('SALVAGE', result.stdout)
        if diag:
            result = run('python3', ROOT / 'buildutils/ufsdiag.py', image)
            self.assertEqual(result.returncode, 0, result.stdout)

    def test_populated_root(self):
        for size in ((128 << 20) + 8192, 512 << 20, 1 << 30):
            with self.subTest(size=size):
                self.grow_check(self.template, size)

    def test_empty_home(self):
        tree = self.work / 'empty'
        tree.mkdir(exist_ok=True)
        image = self.work / 'home.img'
        result = run('makefs', '-t', 'ffs', '-o', 'version=2', '-s', '16m', image, tree)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.grow_check(image, 2 << 30, diag=False)

    def test_invalid_requests_leave_source_unchanged(self):
        image = self.work / 'invalid.img'
        for arg in ('0', '-1', '123junk', str(1 << 80), str(64 << 20), str(256 << 20)):
            shutil.copyfile(self.template, image)
            before = hashlib.sha256(image.read_bytes()).digest()
            result = run(self.binary, image, arg)
            self.assertNotEqual(result.returncode, 0, arg)
            self.assertEqual(hashlib.sha256(image.read_bytes()).digest(), before, arg)

    def test_inconsistent_block_shift_is_refused(self):
        image = self.work / 'bad-shift.img'
        shutil.copyfile(self.template, image)
        loc, _ = geometry(image)
        with open(image, 'r+b') as f:
            f.seek(loc + 80)
            f.write(struct.pack('<i', 0))
            f.truncate(512 << 20)
        result = run(self.binary, image, 512 << 20)
        self.assertNotEqual(result.returncode, 0, result.stdout)

    def test_inode_pointing_into_metadata_is_refused(self):
        image = self.work / 'bad-inode.img'
        shutil.copyfile(self.template, image)
        _, sb = geometry(image)
        iblk = struct.unpack_from('<i', sb, 16)[0]
        fsize = struct.unpack_from('<i', sb, 52)[0]
        with open(image, 'r+b') as f:
            f.seek(iblk * fsize + 2 * 256 + 112)
            f.write(struct.pack('<q', iblk))
            f.truncate(512 << 20)
        result = run(self.binary, image, 512 << 20)
        self.assertNotEqual(result.returncode, 0, result.stdout)

    def test_bad_group_refused_before_writing(self):
        image = self.work / 'corrupt.img'
        shutil.copyfile(self.template, image)
        _, sb = geometry(image)
        offset = struct.unpack_from('<i', sb, 12)[0] * struct.unpack_from('<i', sb, 52)[0]
        with open(image, 'r+b') as f:
            f.seek(offset + 4)
            f.write(b'BAD!')
            f.truncate(512 << 20)
        with open(image, 'rb') as f:
            before = hashlib.sha256(f.read()).digest()
        result = run(self.binary, image, 512 << 20)
        self.assertNotEqual(result.returncode, 0)
        with open(image, 'rb') as f:
            self.assertEqual(hashlib.sha256(f.read()).digest(), before)


if __name__ == '__main__':
    unittest.main()
