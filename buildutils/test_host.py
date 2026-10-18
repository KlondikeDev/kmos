# This file has been generated in whole or in part by a Large Language Model
"""Run sanitizer checks for logging, ACPI, block/page caches and VM faults."""
import os
import shutil
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
        ("e1000_protocol", ["buildutils/tests/e1000_protocol.c"], ["-Ibuildutils/tests/e1000_stubs"]),
        ("iwx200_protocol", ["buildutils/tests/iwx200_protocol.c"], ["-Ibuildutils/tests/iwx200_stubs"]),
        ("random_protocol", ["buildutils/tests/random_protocol.c"], ["-Ibuildutils/tests/random_stubs"]),
        ("random_cpu", ["buildutils/tests/random_cpu.c", "kernel/random.c"], ["-Ibuildutils/tests/random_cpu_stubs"]),
        ("inet_linux_abi", ["buildutils/tests/inet_linux_abi.c"], []),
        ("iwx_station_flow", ["buildutils/tests/iwx_station_flow.c", "kernel/wifi.c", "kernel/wifi_wpa.c", "kernel/wifi_sha1.c", "kernel/wifi_aes.c"], ["-D_GNU_SOURCE"]),
        ("iwx_station_protocol", ["buildutils/tests/iwx_station_protocol.c", "drivers/iwx200_station.c"], []),
        ("wifi_control", ["buildutils/tests/wifi_control.c", "kernel/wifi_control.c"], ["-Ibuildutils/tests/wifi_control_stubs"]),
        ("wifi_protocol", ["buildutils/tests/wifi_protocol.c", "kernel/wifi.c"], []),
        ("gpt", ["buildutils/tests/gpt.c", "kernel/gpt.c"], []),
        ("nvme_protocol", ["buildutils/tests/nvme_protocol.c"], ["-Ibuildutils/tests/nvme_stubs"]),
        ("wifi_crypto", ["buildutils/tests/wifi_crypto.c", "kernel/wifi_sha1.c", "kernel/wifi_aes.c"], []),
        ("wifi_handshake", ["buildutils/tests/wifi_handshake.c", "kernel/wifi_wpa.c", "kernel/wifi_sha1.c", "kernel/wifi_aes.c"], []),        ("pagecache", ["buildutils/tests/pagecache.c", "kernel/pagecache.c"], []),
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

subprocess.run(["sh", "buildutils/tests/inet-host/run.sh"],cwd=root,check=True)

# Persistent settings and fixed-ESP update invariants use ordinary host libc.
with tempfile.TemporaryDirectory(prefix="kunix-remote-tests-") as work:
    for name in ("network_settings", "network_regressions", "update_fat", "update_ownership", "update_interrupt", "update_live"):
        binary = str(Path(work) / name)
        subprocess.run(["gcc", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "buildutils/tests/" + name + ".c",
                        "-o", binary], cwd=root, check=True)
        modes = ("key-race", "disable", "wifi-race") if name == "network_regressions" else (None,)
        for mode in modes:
            subprocess.run([binary, *([mode] if mode else [])],
                           env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"), check=True)
for name in ("update_bundle.py", "selfhost_bundle.py"):
    subprocess.run(["python3", "buildutils/tests/" + name], cwd=root, check=True)

if shutil.which("xorriso"):
    subprocess.run(["python3", "buildutils/tests/update_live_inputs.py"], cwd=root, check=True)
else:
    print("SKIP live ISO input regression: xorriso is not installed")
selfhost_tools = root / "build/selfhost/toolchain/bin"
if all((selfhost_tools / name).is_file() for name in ("gcc", "make", "makefs", "limine")) and (root / "build/binaries/kunix-kodiak.elf").is_file():
    subprocess.run(["python3", "buildutils/tests/selfhost_image.py"], cwd=root, check=True)
    subprocess.run(["python3", "buildutils/tests/selfhost_userland.py"], cwd=root, check=True)
else:
    print("SKIP self-host image fixtures: build the optional self-host tools first")
