# This file has been generated in whole or in part by a Large Language Model
"""Run host checks for kernel log buffering, AML bounds and cache flag walking."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix="kunix-host-tests-") as work:
    for name, sources, includes in [
        ("acpi_aml", ["buildutils/tests/acpi_aml.c", "kernel/acpi_aml.c"], []),
        ("vmm_cache", ["buildutils/tests/vmm_cache.c"], []),
        ("kmsg", ["buildutils/tests/kmsg.c"], ["-Ibuildutils/tests/include"]),
    ]:
        binary = str(Path(work) / name)
        command = ["gcc", *includes, "-Iinclude", "-idirafter", "include/klibc",
                   "-Wall", "-Wextra", "-fno-builtin", "-ffunction-sections", "-fdata-sections",
                   "-Wl,--gc-sections", "-fsanitize=address,undefined", *sources, "-o", binary]
        subprocess.run(command, cwd=root, check=True)
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
        subprocess.run([binary], env=env, check=True)
