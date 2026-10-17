# This file has been generated in whole or in part by a Large Language Model
"""Run sanitizer checks for logging, ACPI, block/page caches and VM faults."""
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
        ("disk_cache", ["buildutils/tests/disk_cache.c"], ["-Ibuildutils/tests/disk_include"]),
        ("gpt", ["buildutils/tests/gpt.c", "kernel/gpt.c"], []),
        ("nvme_protocol", ["buildutils/tests/nvme_protocol.c"], ["-Ibuildutils/tests/nvme_stubs"]),
        ("pagecache", ["buildutils/tests/pagecache.c", "kernel/pagecache.c"], []),
        ("mman_fill", ["buildutils/tests/mman_fill.c", "kernel/pagecache.c"], []),
        ("battery", ["buildutils/tests/battery.c", "kernel/battery.c"], ["-DBATTERY_NORMALIZE_TEST"]),
    ]:
        binary = str(Path(work) / name)
        command = ["gcc", *includes, "-Iinclude", "-idirafter", "include/klibc",
                   "-Wall", "-Wextra", "-fno-builtin", "-ffunction-sections", "-fdata-sections",
                   "-Wl,--gc-sections", "-fsanitize=address,undefined", *sources, "-o", binary]
        subprocess.run(command, cwd=root, check=True)
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
        subprocess.run([binary], env=env, check=True)
        if name == "disk_cache":
            subprocess.run([binary], env=dict(env, KUNIX_HOST_BATCH="1"), check=True)
        if name == "pagecache":
            subprocess.run([binary], env=dict(env, KUNIX_HOST_LARGE_CACHE="1"), check=True)
