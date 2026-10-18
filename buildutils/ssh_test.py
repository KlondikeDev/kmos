#!/usr/bin/env python3
# This file has been generated in whole or in part by a Large Language Model
"""Exercise OpenSSH -> Kunix Dropbear and Kunix dbclient -> host forward -> Dropbear.

Build the live ISO first. All guest credentials and filesystem changes live in
its RAM root; this harness attaches no disk images and starts no host sshd.
"""
import argparse
import http.server
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parent.parent


def fixture_body(path, fixtures):
    """Only explicit fixture names are accessible, never an arbitrary file."""
    return fixtures.get(path) if path in ("/authorized_keys", "/client.key") else None


class FixtureHandler(http.server.BaseHTTPRequestHandler):
    fixtures = {}

    def do_GET(self):
        body = fixture_body(self.path, self.fixtures)
        self.send_response(200 if body is not None else 404)
        self.send_header("Content-Length", str(len(body) if body is not None else 0))
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if body is not None:
            self.wfile.write(body)

    def log_message(self, *_args):
        pass


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def stop_group(process):
    # test.py and its QEMU child have their own process group. Clean up both
    # on an exception even if the harness cannot reach its Python finally.
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        pass
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait(timeout=5)


def run_with_open_stdin(command, **kwargs):
    # An inherited EOF makes OpenSSH send channel EOF before the guest child
    # claims its PTY. Keep the write end open until the remote command exits.
    reader, writer = os.pipe()
    try:
        return subprocess.run(command, stdin=reader, **kwargs)
    finally:
        os.close(reader)
        os.close(writer)


def run(args):
    ssh = shutil.which("ssh")
    keygen = shutil.which("ssh-keygen")
    dropbearkey = ROOT / "build/phase4-toolchain/bin/dropbearkey"
    if not ssh or not keygen or not dropbearkey.is_file():
        raise RuntimeError("requires host ssh/ssh-keygen and built phase4-toolchain/bin/dropbearkey")
    iso = Path(args.iso).resolve()
    if not iso.is_file():
        raise RuntimeError(f"live ISO is missing: {iso}")
    base = ROOT / "build/phase4/ssh-test"
    base.mkdir(parents=True, exist_ok=True)
    serial = base / "serial-live.log"
    serial.write_text("")
    port = args.host_port or free_port()
    # Private keys are created only under this disposable directory and are
    # deleted after the VM exits. No real ~/.ssh paths are opened or changed.
    with tempfile.TemporaryDirectory(prefix="credentials-", dir=base) as temporary:
        work = Path(temporary)
        os.chmod(work, 0o700)
        identity = work / "openssh.key"
        client = work / "client.key"
        subprocess.run([keygen, "-q", "-t", "ed25519", "-N", "", "-C", "kunix-disposable-ssh-test", "-f", str(identity)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=30)
        generated = subprocess.run([str(dropbearkey), "-t", "ed25519", "-f", str(client)],
                                   check=True, capture_output=True, text=True, timeout=30)
        client_public = next((line for line in generated.stdout.splitlines() if line.startswith("ssh-ed25519 ")), None)
        if not client_public:
            raise RuntimeError("dropbearkey did not return its public key")
        os.chmod(identity, 0o600)
        os.chmod(client, 0o600)
        FixtureHandler.fixtures = {
            "/authorized_keys": identity.with_suffix(".key.pub").read_bytes() + (client_public + "\n").encode(),
            "/client.key": client.read_bytes(),
        }
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 18082), FixtureHandler)
        server.daemon_threads = True
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            command = [sys.executable, str(ROOT / "buildutils/test.py"), "--iso", str(iso), "--no-ata",
                       "--mem", "512M", "--timeout", str(args.timeout), "--network",
                       "--hostfwd", f"tcp:127.0.0.1:{port}-:2222", "--serial-live", str(serial),
                       "--log", str(base / "guest.log"), "--command", f"sshtest {port}"]
            with (base / "harness.log").open("w") as log:
                process = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
                try:
                    deadline = time.monotonic() + args.timeout + 20
                    while time.monotonic() < deadline:
                        if "sshtest: server ready" in serial.read_text(errors="replace"):
                            break
                        if process.poll() is not None:
                            raise RuntimeError("VM test exited before its SSH server became ready")
                        time.sleep(0.1)
                    else:
                        raise RuntimeError("timed out waiting for the guest SSH server")
                    inbound = run_with_open_stdin([
                        ssh, "-F", "/dev/null", "-tt", "-p", str(port), "-i", str(identity),
                        "-o", "IdentitiesOnly=yes", "-o", "IdentityAgent=none", "-o", "BatchMode=yes",
                        "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
                        "-o", "GlobalKnownHostsFile=/dev/null", "-o", "ConnectTimeout=10", "-o", "LogLevel=ERROR",
                        "root@127.0.0.1", "test -t 0 && printf inbound-ok > /tmp/ssh-inbound-ok"],
                        capture_output=True, timeout=45)
                    (base / "openssh.log").write_bytes(inbound.stdout+inbound.stderr)
                    if inbound.returncode:
                        raise RuntimeError(f"host OpenSSH authentication/command failed (exit {inbound.returncode})")
                    result = process.wait(timeout=max(1, deadline - time.monotonic()))
                    text = serial.read_text(errors="replace")
                    summary = re.search(r"sshtest: (\d+) passed, (\d+) failed", text)
                    if result or not summary or int(summary[2]) or int(summary[1]) < 5:
                        raise RuntimeError("guest SSH checks failed; inspect guest.log and harness.log")
                    print(f"SSH interoperability: {summary[1]} guest checks passed; host OpenSSH inbound and guest dbclient outbound PASS")
                finally:
                    stop_group(process)
        finally:
            server.shutdown()
            server.server_close()
            worker.join(timeout=2)
            FixtureHandler.fixtures = {}


class FixtureTests(unittest.TestCase):
    def test_remote_command_stdin_remains_open(self):
        # SSH must not send channel EOF before the guest sets up its PTY.
        probe = [sys.executable, "-c",
                 "import select,sys; print('eof' if select.select([sys.stdin],[],[],0.05)[0] else 'open')"]
        result = run_with_open_stdin(probe, capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout.strip(), "open")

    def test_explicit_credentials_only(self):
        fixtures = {"/authorized_keys": b"public", "/client.key": b"disposable-private"}
        self.assertEqual(fixture_body("/authorized_keys", fixtures), b"public")
        self.assertEqual(fixture_body("/client.key", fixtures), b"disposable-private")
        for path in ("/", "/../client.key", "/client.key?x=1", "/etc/passwd", "/%2e%2e/client.key"):
            self.assertIsNone(fixture_body(path, fixtures))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--iso", default=str(ROOT / "build/binaries/kunix-live.iso"))
    parser.add_argument("--timeout", type=float, default=240)
    parser.add_argument("--host-port", type=int)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        unittest.main(argv=[sys.argv[0]])
    else:
        if args.host_port is not None and not 1024 <= args.host_port <= 65535:
            parser.error("--host-port must be between 1024 and 65535")
        if args.timeout <= 0:
            parser.error("--timeout must be positive")
        try:
            run(args)
        except (OSError, RuntimeError, subprocess.SubprocessError) as error:
            sys.exit(f"SSH test failed: {error}")
