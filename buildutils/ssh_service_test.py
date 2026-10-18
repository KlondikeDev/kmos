#!/usr/bin/env python3
# This file has been generated in whole or in part by a Large Language Model
"""Test Habilis inbound SSH enable/disable and persistent identity across two boots."""
import argparse
import http.server
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest

from ssh_test import ROOT, FixtureHandler, free_port, run_with_open_stdin, stop_group
from test import validate_home_image


def openssh_command(ssh, identity, known_hosts, port, first_boot):
    return [ssh, "-F", "/dev/null", "-tt", "-p", str(port), "-i", str(identity),
            "-o", "IdentitiesOnly=yes", "-o", "IdentityAgent=none", "-o", "BatchMode=yes",
            "-o", "StrictHostKeyChecking=" + ("accept-new" if first_boot else "yes"),
            "-o", f"UserKnownHostsFile={known_hosts}", "-o", "GlobalKnownHostsFile=/dev/null",
            "-o", "ConnectTimeout=10", "-o", "LogLevel=ERROR", "root@127.0.0.1",
            "test -t 0 && printf inbound-ok > /tmp/ssh-service-inbound-ok"]


def run(args):
    ssh, keygen, makefs = (shutil.which(name) for name in ("ssh", "ssh-keygen", "makefs"))
    if not all((ssh, keygen, makefs)):
        raise RuntimeError("requires ssh, ssh-keygen and makefs on the host")
    iso = Path(args.iso).resolve()
    if not iso.is_file():
        raise RuntimeError(f"missing live ISO: {iso}")
    base = ROOT / "build/phase4/ssh-service-test"
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="disposable-", dir=base) as temporary:
        work = Path(temporary)
        os.chmod(work, 0o700)
        identity, known_hosts = work / "identity", work / "known_hosts"
        subprocess.run([keygen, "-q", "-t", "ed25519", "-N", "", "-C", "kunix-service-test", "-f", str(identity)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=30)
        seed = work / "seed"
        (seed / "root").mkdir(parents=True, mode=0o700)
        os.chmod(seed, 0o755)
        home = work / "home.img"
        subprocess.run([makefs, "-t", "ffs", "-o", "version=2", "-s", "64m", "-f", "4096", str(home), str(seed)],
                       check=True, capture_output=True, timeout=60)
        subprocess.run([sys.executable, str(ROOT / "buildutils/ufschown.py"), str(home), "0", "0"],
                       check=True, capture_output=True, timeout=30)
        FixtureHandler.fixtures = {"/authorized_keys": identity.with_suffix(".pub").read_bytes()}
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), FixtureHandler)
        server.daemon_threads = True
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        host_port = free_port()
        fixture_port = server.server_address[1]
        try:
            for phase in (1, 2):
                serial = base / f"serial-{phase}.log"
                serial.write_text("")
                command = [sys.executable, str(ROOT / "buildutils/test.py"), "--iso", str(iso), "--no-ata",
                           "--home-image", str(home), "--mem", "512M", "--timeout", str(args.timeout), "--network",
                           "--hostfwd", f"tcp:127.0.0.1:{host_port}-:22", "--serial-live", str(serial),
                           "--log", str(base / f"guest-{phase}.log"), "--command", f"sshservicetest {phase} {fixture_port}"]
                with (base / f"harness-{phase}.log").open("w") as log:
                    process = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
                    try:
                        deadline = time.monotonic() + args.timeout + 20
                        ready = f"sshservicetest: server ready phase {phase}"
                        while time.monotonic() < deadline:
                            if ready in serial.read_text(errors="replace"):
                                break
                            if process.poll() is not None:
                                raise RuntimeError(f"boot {phase}: guest exited before service readiness; inspect {base}")
                            time.sleep(0.1)
                        else:
                            raise RuntimeError(f"boot {phase}: service readiness timed out; inspect {base}")
                        result = run_with_open_stdin(openssh_command(ssh, identity, known_hosts, host_port, phase == 1),
                                                     capture_output=True, timeout=45)
                        (base / f"openssh-{phase}.log").write_bytes(result.stdout + result.stderr)
                        if result.returncode:
                            raise RuntimeError(f"boot {phase}: inbound SSH/host identity verification failed (exit {result.returncode})")
                        if phase == 1:
                            fingerprint = known_hosts.read_bytes()
                        elif known_hosts.read_bytes() != fingerprint:
                            raise RuntimeError("host identity changed across reboot")
                        code = process.wait(timeout=max(1, deadline - time.monotonic()))
                        summary = re.search(r"sshservicetest: (\d+) passed, (\d+) failed", serial.read_text(errors="replace"))
                        if code or not summary or int(summary[2]) or int(summary[1]) < 9:
                            raise RuntimeError(f"boot {phase}: guest service checks failed; inspect {base}")
                        print(f"Habilis SSH boot {phase}: {summary[1]} checks passed; OpenSSH PTY and persistent identity PASS", flush=True)
                    finally:
                        stop_group(process)
        finally:
            server.shutdown()
            server.server_close()
            worker.join(timeout=2)
            FixtureHandler.fixtures = {}


class HarnessTests(unittest.TestCase):
    def test_second_boot_requires_saved_host_identity(self):
        command = openssh_command("ssh", "identity", "fixture_known_hosts", 2222, False)
        self.assertIn("StrictHostKeyChecking=yes", command)
        self.assertIn("UserKnownHostsFile=fixture_known_hosts", command)
        self.assertNotIn("StrictHostKeyChecking=no", command)

    def test_actual_home_and_alias_are_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            actual = root / "home.img"
            actual.write_bytes(b"real")
            alias = root / "alias.img"
            os.link(actual, alias)
            for path in (actual, alias):
                with self.assertRaises(ValueError):
                    validate_home_image(root, path)
            fixture = root / "disposable.img"
            fixture.write_bytes(b"test")
            self.assertEqual(validate_home_image(root, fixture), str(fixture))
            symbolic = root / "symbolic.img"
            symbolic.symlink_to(fixture)
            with self.assertRaises(ValueError):
                validate_home_image(root, symbolic)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", default=str(ROOT / "build/binaries/kunix-live.iso"))
    parser.add_argument("--timeout", type=float, default=240)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        unittest.main(argv=[sys.argv[0]])
    elif args.timeout <= 0:
        parser.error("--timeout must be positive")
    else:
        try:
            run(args)
        except (OSError, RuntimeError, subprocess.SubprocessError) as error:
            sys.exit(f"SSH service test failed: {error}")
