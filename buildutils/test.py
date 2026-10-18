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
import atexit
import os
import re
import socket
import stat
import subprocess
import sys
import shutil
import tempfile
import time

# ktest: the native syscall suite. pmtest: Linux programs through Prime
# Meridian (musl). pmsmoke: zsh and busybox, as a user drives them.
# permtest: users and permissions (root dropping to an ordinary user).
# fbtest: the framebuffer, mouse and raw keyboard, as the X server uses them.
# xtest: X itself -- the server, an xterm typed into, the console handed back.
# ufs: the UFS2 driver -- one boot writes a tree, a second boot checks and
# removes it, then the host checks the image (ufsdiag.py, and FreeBSD's
# fsck_ffs if buildutils/fsck-ffs.sh has built it).
SUITES = ["ktest", "pmtest", "pmsmoke", "permtest", "fbtest", "xtest", "vmtest", "ufs"]
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


def validate_home_image(root, image):
    """An explicit disposable fixture survives test boots; never attach the user's home."""
    if not stat.S_ISREG(os.lstat(image).st_mode):
        raise ValueError("--home-image requires a regular file, not a symlink or device")
    actual = os.path.join(root, "home.img")
    if os.path.exists(actual) and os.path.samefile(actual, image):
        raise ValueError("--home-image refuses the repository's real home.img; create a disposable fixture")
    return os.path.abspath(image)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--mem", default="256M", help="guest RAM (default 256M)")
    ap.add_argument("--timeout", type=float, default=180, help="seconds to wait")
    ap.add_argument("--command", default="ktest", help="what to type at the shell")
    ap.add_argument("--log", help="also save the whole serial log here")
    ap.add_argument("--iso", help="boot a different ISO (for display mode and live image checks)")
    ap.add_argument("--cpu", default="max", help="QEMU CPU (max exposes hardware entropy)")
    ap.add_argument("--network", action="store_true", help="attach e1000 with QEMU user networking")
    ap.add_argument("--hostfwd", action="append", default=[], help="QEMU localhost forwarding rule")
    ap.add_argument("--serial-live", help="write guest serial to this file while the VM runs")
    ap.add_argument("--machine", default="pc", help="QEMU machine type (default pc)")
    ap.add_argument("--firmware", help="boot with this firmware ROM, e.g. OVMF_CODE_4M.fd")
    ap.add_argument("--firmware-vars", help="variable-store template for split OVMF firmware (copied per VM)")
    ap.add_argument("--acpi-table", action="append", default=[], help="add an ACPI table to QEMU (repeatable)")
    ap.add_argument("--nvme", action="append", default=[], help="attach a disposable raw NVMe image")
    ap.add_argument("--nvme-block-size", type=int, choices=[512,4096], default=512)
    ap.add_argument("--home-image", help="attach this disposable home image read/write, preserving it across boots")
    ap.add_argument("--no-ata", action="store_true", help="omit legacy disk images")
    ap.add_argument("--boot-nvme", action="store_true", help="boot firmware from NVMe without the ISO")
    ap.add_argument("--install-answer", choices=["YES","NO"], help="answer the installer in this disposable VM")
    ap.add_argument("--expect-boot-error", help="expect this refusal marker instead of a shell")
    ap.add_argument("--confirm-install", action="store_true", help="type YES for the installer inside this VM")
    ap.add_argument("--expect-power", choices=["off", "restart"],
                    help="expect the command to power off or reset QEMU instead of a test summary")
    ap.add_argument("--all", action="store_true",
                    help="run every suite (" + ", ".join(SUITES) + "), each in a fresh boot")
    ap.add_argument("--suite", help="run one suite by name (as --all would)")
    args = ap.parse_args()
    if args.firmware_vars and not args.firmware:
        ap.error("--firmware-vars requires --firmware")
    for image in args.nvme:
        if not stat.S_ISREG(os.stat(image).st_mode):
            ap.error("--nvme accepts regular VM image files only")

    def boot(command):
        extra = ["--machine", args.machine,"--cpu",args.cpu]
        if args.network: extra += ["--network"]
        for forward in args.hostfwd: extra += ["--hostfwd",forward]
        if args.iso: extra += ["--iso", args.iso]
        if args.firmware: extra += ["--firmware", args.firmware]
        if args.firmware_vars: extra += ["--firmware-vars", args.firmware_vars]
        for image in args.nvme: extra += ["--nvme",image]
        extra += ["--nvme-block-size",str(args.nvme_block_size)]
        if args.home_image: extra += ["--home-image", args.home_image]
        if args.no_ata: extra += ["--no-ata"]
        if args.boot_nvme: extra += ["--boot-nvme"]
        for table in args.acpi_table: extra += ["--acpi-table", table]
        return subprocess.run([sys.executable, os.path.abspath(__file__), "--mem", args.mem,
                               "--timeout", str(args.timeout), "--command", command, *extra]).returncode == 0

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
    iso = args.iso or os.path.join(root, "build/binaries/kunix-kodiak.iso")
    if not args.boot_nvme and not os.path.exists(iso):
        sys.exit(f"no ISO at {iso}: build first")

    work = tempfile.mkdtemp(prefix="kunix-test-")
    # Every VM gets a private 128 MiB home disk. Retaining it after each
    # suite fills /tmp during repeated runs; --log preserves diagnostics.
    atexit.register(shutil.rmtree, work, ignore_errors=True)
    serial = os.path.abspath(args.serial_live) if args.serial_live else os.path.join(work,"serial.log")
    if args.serial_live:
        if os.path.lexists(serial) and not stat.S_ISREG(os.stat(serial).st_mode):
            ap.error("--serial-live must name a regular output file")
        with open(serial,"w"): pass
    monitor = os.path.join(work, "monitor.sock")
    drives = [] if args.no_ata else ["-drive", f"file={root}/disk.img,format=raw,if=ide,index=0"]
    if not args.no_ata and os.path.exists(os.path.join(root, "ufs.img")):
        drives += ["-drive", f"file={root}/ufs.img,format=raw,if=ide,index=1"]
    # The home disk is yours, not the tests': they get a copy, checked after.
    home = None
    if args.home_image:
        try:
            home = validate_home_image(root, args.home_image)
        except (ValueError, OSError) as error:
            ap.error(str(error))
        drives += ["-drive", f"file={home},format=raw,if=ide,index=3"]
    elif not args.no_ata and os.path.exists(os.path.join(root, "home.img")):
        home = os.path.join(work, "home.img")
        shutil.copyfile(os.path.join(root, "home.img"), home)
        drives += ["-drive", f"file={home},format=raw,if=ide,index=3"]

    extra = ["-machine",args.machine,"-cpu",args.cpu]
    if args.network:
        network="user,id=kunixnet"
        for forward in args.hostfwd:
            if not forward.startswith(("tcp:127.0.0.1:","udp:127.0.0.1:")):
                ap.error("--hostfwd must bind 127.0.0.1")
            network+=",hostfwd="+forward
        extra += ["-netdev",network,"-device","e1000,netdev=kunixnet"]
    elif args.hostfwd:
        ap.error("--hostfwd requires --network")
    else:
        extra += ["-nic","none"]
    for i,path in enumerate(args.nvme):
        drives += ["-drive", f"file={os.path.abspath(path)},format=raw,if=none,id=nvme{i}",
                   "-device",f"nvme,drive=nvme{i},serial=KUNIXTEST{i},logical_block_size={args.nvme_block_size},physical_block_size={args.nvme_block_size}"]
    boot_media=[] if args.boot_nvme else ["-cdrom",iso,"-boot","d"]
    if args.firmware_vars:
        varstore = os.path.join(work, "firmware-vars.fd")
        shutil.copyfile(args.firmware_vars, varstore)
        extra += ["-drive", f"if=pflash,format=raw,readonly=on,file={args.firmware}",
                  "-drive", f"if=pflash,format=raw,file={varstore}"]
    elif args.firmware:
        extra += ["-bios", args.firmware]
    if args.expect_power: extra += ["-no-reboot"]
    for table in args.acpi_table: extra += ["-acpitable", f"file={table}"]
    qemu = subprocess.Popen(
        ["qemu-system-x86_64", *extra, *boot_media, *drives,
         "-serial", f"file:{serial}", "-monitor", f"unix:{monitor},server,nowait",
         "-display", "none", "-m", args.mem],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    boot_start = time.monotonic()

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
        if args.expect_boot_error:
            refused=wait_for(re.escape(args.expect_boot_error),deadline)
            print("PASS boot refusal" if refused else "FAIL boot refusal")
            sys.exit(0 if refused else 1)
        started = wait_for(r"Started (\S+) as pid 1", deadline)
        # Under Habilis the console comes once boot is done (it says so),
        # and it is a login prompt: root, who has no password by default.
        habilis = started and started.group(1).endswith("init")
        if habilis:
            started = wait_for(r"habilis: started console", deadline)
        if not started:
            print(log()[-2000:])
            sys.exit("kernel did not reach the shell")
        print(f"boot: console ready in {time.monotonic() - boot_start:.3f} seconds", flush=True)
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
        x_started = None
        x_elapsed = None
        install_confirmed = False
        while time.time() < deadline and qemu.poll() is None:
            out = log()
            if "KERNEL PANIC!" in out:
                break
            # Guest PIT ticks can be lost during polled disk I/O with IRQs
            # disabled. Compare X startup using host wall time instead.
            if args.command == "xtest":
                if x_started is None and "xtest: X timing start" in out:
                    x_started = time.monotonic()
                if x_elapsed is None and x_started is not None and "xtest: X startup" in out:
                    x_elapsed = time.monotonic() - x_started
                    print(f"X: startup wall time {x_elapsed:.3f} seconds", flush=True)
            if (args.confirm_install or args.install_answer) and not install_confirmed and ("install: confirmation required" in out or "storage: confirmation required" in out):
                if not args.nvme: raise RuntimeError("installer confirmation requires an explicit NVMe VM image")
                answer=args.install_answer or "YES"
                for key in ["shift-"+c.lower() for c in answer]+["ret"]:
                    mon.sendall(f"sendkey {key}\n".encode());time.sleep(0.1)
                install_confirmed=True
            requests = re.findall(r"^monitor: (.+?)\r?$", out, re.M)
            for c in requests[sent:]:
                mon.sendall((c + "\n").encode())
                time.sleep(0.05)
            sent = len(requests)
            if args.install_answer == "NO" and "install: canceled" in out:
                break
            summary = re.search(r"\w+: (\d+) passed, (\d+) failed", out)
            if summary:
                break
            time.sleep(0.01 if args.command == "xtest" else 0.2)
        out = log()
        if args.install_answer=="NO":
            canceled="install: canceled" in out
            print("PASS installer cancellation" if canceled else "FAIL installer cancellation")
            sys.exit(0 if canceled else 1)
        if args.expect_power:
            marker = "power: switching off" if args.expect_power == "off" else "power: restarting"
            acpi_marker = "acpi: entering S5" if args.expect_power == "off" else "acpi: issuing firmware reset"
            ok = (qemu.poll() == 0 and marker in out and acpi_marker in out and
                  "power: using fallback" not in out)
            print(f"{'PASS' if ok else 'FAIL'} ACPI {args.expect_power} on {args.machine}")
            if not ok: print(out[-2000:])
            sys.exit(0 if ok else 1)
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
