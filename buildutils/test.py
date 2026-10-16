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
    python3 buildutils/test.py --command pmtest   # Prime Meridian's suite
    python3 buildutils/test.py --all              # every suite, each in a fresh boot
    python3 buildutils/test.py --suite ufs        # just the UFS2 write/reboot/fsck test

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
import shutil
import tempfile
import time

# ktest: the native syscall suite. pmtest: Linux programs through Prime
# Meridian (musl). pmsmoke: zsh and busybox, as a user drives them.
# permtest: users and permissions (root dropping to an ordinary user).
# fbtest: the framebuffer, mouse and raw keyboard, as the X server uses them.
# ufs: the UFS2 driver -- one boot writes a tree, a second boot checks and
# removes it, then the host checks the image (ufsdiag.py, and FreeBSD's
# fsck_ffs if buildutils/fsck-ffs.sh has built it).
SUITES = ["ktest", "pmtest", "pmsmoke", "permtest", "fbtest", "ufs"]
UFS_PHASES = ["ufstest write", "ufstest verify"]
FSCK = os.path.expanduser("~/opt/kunix-host/bin/fsck_ffs")


def check_ufs_image(root, img=None, quiet=False):
    """True if the host-side checkers find nothing wrong with a UFS2 image
    (the root image, ufs.img, unless told otherwise)."""
    img = img or os.path.join(root, "ufs.img")
    diag = subprocess.run([sys.executable, os.path.join(root, "buildutils/ufsdiag.py"), img],
                          capture_output=quiet, text=True)
    ok = diag.returncode == 0
    if quiet and not ok:
        print(f"{img}:\n{diag.stdout}")
    if os.path.exists(FSCK):
        out = subprocess.run([FSCK, "-n", img], capture_output=True, text=True).stdout
        # A clean check prints only its phases and the summary line.
        noise = [l for l in out.splitlines() if l.strip() and not l.startswith("**")
                 and "is not a disk device" not in l and not l.startswith("CONTINUE")
                 and " files, " not in l]
        if not quiet or noise:
            print(f"fsck_ffs {os.path.basename(img)}: " + ("clean" if not noise else "PROBLEMS:\n" + "\n".join(noise)))
        ok = ok and not noise
    elif not quiet:
        print("fsck_ffs not built (buildutils/fsck-ffs.sh); only ufsdiag.py checked the image")
    return ok

KEYS = {" ": "spc", "\n": "ret", ".": "dot", "/": "slash", "-": "minus"}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--mem", default="256M", help="guest RAM (default 256M)")
    ap.add_argument("--timeout", type=float, default=180, help="seconds to wait")
    ap.add_argument("--command", default="ktest", help="what to type at the shell")
    ap.add_argument("--log", help="also save the whole serial log here")
    ap.add_argument("--all", action="store_true",
                    help="run every suite (" + ", ".join(SUITES) + "), each in a fresh boot")
    ap.add_argument("--suite", help="run one suite by name (as --all would)")
    args = ap.parse_args()

    def boot(command):
        return subprocess.run([sys.executable, os.path.abspath(__file__), "--mem", args.mem,
                               "--timeout", str(args.timeout), "--command", command]).returncode == 0

    def run_suite(suite):
        if suite != "ufs":
            return boot(suite)
        ok = all(boot(phase) for phase in UFS_PHASES)
        return check_ufs_image(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))) and ok

    if args.suite:
        sys.exit(0 if run_suite(args.suite) else 1)
    if args.all:
        failures = 0
        for suite in SUITES:
            print(f"==== {suite}", flush=True)
            failures += not run_suite(suite)
        print(f"\n{len(SUITES) - failures} of {len(SUITES)} suites passed")
        sys.exit(1 if failures else 0)

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
    # The home disk is yours, not the tests': they get a copy, checked after.
    home = None
    if os.path.exists(os.path.join(root, "home.img")):
        home = os.path.join(work, "home.img")
        shutil.copyfile(os.path.join(root, "home.img"), home)
        drives += ["-drive", f"file={home},format=raw,if=ide,index=3"]

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
        started = wait_for(r"Started (\S+) as pid 1", deadline)
        # Under Habilis the console comes once boot is done (it says so),
        # and it is a login prompt: root, who has no password by default.
        habilis = started and started.group(1).endswith("init")
        if habilis:
            started = wait_for(r"habilis: started console", deadline)
        if not started:
            print(log()[-2000:])
            sys.exit("kernel did not reach the shell")
        time.sleep(2.0)                          # let the prompt settle (login flushes typeahead)

        mon = socket.socket(socket.AF_UNIX)
        mon.connect(monitor)

        def type_line(text):
            for ch in text + "\n":
                mon.sendall(f"sendkey {KEYS.get(ch, ch)}\n".encode())
                time.sleep(0.03)

        if habilis:
            type_line("root")
            time.sleep(2.5)                      # login, then zsh starting up
        type_line(args.command)

        # A test may need input only the machine can give -- the mouse
        # moving, keys pressed while it reads them raw. It asks with a serial
        # line "monitor: <command>", which goes to QEMU's monitor as is.
        sent = 0
        summary = None
        while time.time() < deadline and qemu.poll() is None:
            out = log()
            requests = re.findall(r"^monitor: (.+?)\r?$", out, re.M)
            for c in requests[sent:]:
                mon.sendall((c + "\n").encode())
                time.sleep(0.05)
            sent = len(requests)
            summary = re.search(r"\w+: (\d+) passed, (\d+) failed", out)
            if summary:
                break
            time.sleep(0.2)
        out = log()
        for line in out.splitlines():
            if line.startswith(("PASS ", "FAIL ")) or "PANIC" in line:
                print(line)
        if not summary:
            sys.exit("no summary: the suite hung or the kernel died (see above)")
        passed, failed = int(summary.group(1)), int(summary.group(2))
        print(f"\n{passed} passed, {failed} failed")
        time.sleep(1.5)                          # let the shell finish its writes (history)
        qemu.kill()
        qemu.wait()
        if home and not check_ufs_image(root, home, quiet=True):
            print("the home disk was left inconsistent")
            failed += 1
        sys.exit(1 if failed else 0)
    finally:
        qemu.kill()
        qemu.wait()
        if args.log:
            with open(args.log, "w") as f:
                f.write(log())


if __name__ == "__main__":
    main()
