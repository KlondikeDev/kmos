# This file has been generated in whole or in part by a Large Language Model
"""The hosted compiler must use Kunix's global canary, never Linux TLS."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='kunix-selfhost-flags-') as work:
    work = Path(work)
    subprocess.run(['python3', str(ROOT / 'buildutils/selfhost.py'), 'stage', str(work),
                    '--toolchain', str(ROOT / 'build/selfhost/toolchain')], check=True)
    source = work / 'guard.c'
    source.write_text('int guard(int n) { volatile char buf[64]; buf[0] = n; return buf[0]; }\n')
    subprocess.run([str(work / 'opt/selfhost/kernel-bin/x86_64-elf-gcc'), '-c', str(source),
                    '-o', str(work / 'guard.o'), '-ffreestanding', '-mcmodel=kernel',
                    '-O2', '-fstack-protector-strong'], check=True)
    assembly = subprocess.check_output(['objdump', '-dr', str(work / 'guard.o')], text=True)
    assert '__stack_chk_guard' in assembly, assembly
    assert '%gs:0x28' not in assembly and '%fs:0x28' not in assembly, assembly
print('selfhost kernel: global stack canary verified')
