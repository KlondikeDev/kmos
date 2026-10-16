#!/usr/bin/env python3
# This file has been generated in whole or in part by a Large Language Model

# Copyright (c) 2026 Joseph Jones
# Licensed under the Kunix License v1.0 or later
# See LICENSE for details

"""Boot Kunix headless in QEMU, run the ktest suite, and report.

Build first (buildutils/build.sh, or with its final `qemu` step skipped),
then run from the repository root:

    python3 buildutils/test.py            # the whole suite
    python3 buildutils/test.py --mem 48M  # on a smaller machine
    python3 buildutils/test.py --command /ufs/bin/pmtest   # Prime Meridian's suite

Each suite writes its results to /dev/serial; this script types the command
into the shell through QEMU's monitor, follows the serial log until the
"<name>: N passed, M failed" line appears, and exits 0 only if every check
passed.
"""

import argparse
import os
import re
import socket
import subprocess
import sys
import tempfile
import time

KEYS = {" ": "spc", "\n": "ret", ".": "dot", "/": "slash", "-": "minus"}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--mem", default="256M", help="guest RAM (default 256M)")
    ap.add_argument("--timeout", type=float, default=180, help="seconds to wait")
    ap.add_argument("--command", default="ktest", help="what to type at the shell")
    ap.add_argument("--log", help="also save the whole serial log here")
    args = ap.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    iso = os.path.join(root, "build/binaries/kunix-kodiak.iso")
    if not os.path.exists(iso):
        sys.exit(f"no ISO at {iso}: build first")

    work = tempfile.mkdtemp(prefix="kunix-test-")
    serial = os.path.join(work, "serial.log")
    monitor = os.path.join(work, "monitor.sock")
    drives = ["-drive", f"file={root}/disk.img,format=raw,if=ide,index=0"]
    if os.path.exists(os.path.join(root, "ufs.img")):
        drives += ["-drive", f"file={root}/ufs.img,format=raw,if=ide,index=1"]

    qemu = subprocess.Popen(
        ["qemu-system-x86_64", "-cdrom", iso, "-boot", "d", *drives,
         "-serial", f"file:{serial}", "-monitor", f"unix:{monitor},server,nowait",
         "-display", "none", "-m", args.mem],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def log():
        try:
            with open(serial, "rb") as f:
                return f.read().decode("latin-1")
        except FileNotFoundError:
            return ""

    def wait_for(pattern, deadline):
        while time.time() < deadline:
            m = re.search(pattern, log())
            if m:
                return m
            if qemu.poll() is not None:
                return None
            time.sleep(0.2)
        return None

    try:
        deadline = time.time() + args.timeout
        if not wait_for(r"Started \S+ as pid 1", deadline):
            print(log()[-2000:])
            sys.exit("kernel did not reach the shell")
        time.sleep(1.0)                          # let the prompt settle

        mon = socket.socket(socket.AF_UNIX)
        mon.connect(monitor)
        for ch in args.command + "\n":
            mon.sendall(f"sendkey {KEYS.get(ch, ch)}\n".encode())
            time.sleep(0.03)

        summary = wait_for(r"\w+: (\d+) passed, (\d+) failed", deadline)
        out = log()
        for line in out.splitlines():
            if line.startswith(("PASS ", "FAIL ")) or "PANIC" in line:
                print(line)
        if not summary:
            sys.exit("no summary: the suite hung or the kernel died (see above)")
        passed, failed = int(summary.group(1)), int(summary.group(2))
        print(f"\n{passed} passed, {failed} failed")
        sys.exit(1 if failed else 0)
    finally:
        qemu.kill()
        qemu.wait()
        if args.log:
            with open(args.log, "w") as f:
                f.write(log())


if __name__ == "__main__":
    main()
