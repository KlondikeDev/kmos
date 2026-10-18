# This file has been generated in whole or in part by a Large Language Model
"""Check that a self-host source bundle uses the actual build and needs no host paths."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class SourceBundle(unittest.TestCase):
    def test_repeated_staging_refreshes_tools_and_preserves_absolute_links(self):
        with tempfile.TemporaryDirectory() as work:
            work = Path(work)
            toolchain = work / 'tools'
            (toolchain / 'bin').mkdir(parents=True)
            program = work / 'test.c'
            program.write_text('int main(void) { return 0; }\n')
            subprocess.run(['gcc', '-static', '-no-pie', str(program), '-o', str(toolchain / 'bin/gcc')], check=True)
            (toolchain / 'bin/make').write_bytes((toolchain / 'bin/gcc').read_bytes())
            for _ in range(2):
                subprocess.run(['python3', str(ROOT / 'buildutils/selfhost.py'), 'stage',
                                str(work / 'root'), '--toolchain', str(toolchain)], check=True, capture_output=True)
            self.assertEqual((work / 'root/bin/gcc').readlink(), Path('/opt/selfhost/bin/gcc'))
            self.assertTrue((work / 'root/usr/src/kunix/network/netconfig.c').is_file())
            self.assertTrue((work / 'root/usr/src/kunix/system/update.c').is_file())

    def test_static_pie_tool_is_rejected_before_guest_boot(self):
        with tempfile.TemporaryDirectory() as work:
            work = Path(work)
            (work / 'bin').mkdir()
            program = work / 'test.c'
            program.write_text('int main(void) { return 0; }\n')
            subprocess.run(['gcc', '-static-pie', str(program), '-o', str(work / 'bin/gcc')], check=True)
            (work / 'bin/make').write_bytes((work / 'bin/gcc').read_bytes())
            result = subprocess.run(['python3', str(ROOT / 'buildutils/selfhost.py'), 'stage',
                                     str(work.parent / (work.name + '-root')), '--toolchain', str(work)], capture_output=True, text=True)
            import shutil
            shutil.rmtree(work.parent / (work.name + '-root'), ignore_errors=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('static PIE', result.stderr)

    def test_bundle_has_sources_and_freestanding_build_recipe(self):
        with tempfile.TemporaryDirectory() as work:
            out = Path(work) / 'source'
            subprocess.run(['python3', str(ROOT / 'buildutils/selfhost.py'), 'source', str(out)], check=True)
            self.assertTrue((out / 'kernel/kernel.c').is_file())
            self.assertTrue((out / 'vendor/lwip/src/core/tcp.c').is_file())
            self.assertTrue((out / 'vendor/uacpi/source/interpreter.c').is_file())
            self.assertFalse((out / '.git').exists())
            self.assertFalse(list(out.rglob('*.o')))
            recipe = (out / 'buildutils/selfhost-commands.sh').read_text()
            self.assertNotIn('python3', recipe)
            self.assertNotIn('/home/joseph', recipe)
            self.assertIn('vendor/lwip/src/core/', recipe)
            self.assertIn('buildutils/linker.ld', recipe)
            subprocess.run(['sh', '-n', str(out / 'buildutils/selfhost-commands.sh')], check=True)
            subprocess.run(['sh', '-n', str(out / 'buildutils/selfhost-build.sh')], check=True)
            result = subprocess.run(['make', '-n', '-f', 'buildutils/selfhost.mk', 'kernel'], cwd=out, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('selfhost-build.sh kernel', result.stdout)
            self.assertIn('KUNIX_BUILD_ID', (out / 'build/generated/kunix_build.h').read_text())
            self.assertIn('SHA256', (out / 'SOURCE-PROVENANCE').read_text())

if __name__ == '__main__':
    unittest.main()
