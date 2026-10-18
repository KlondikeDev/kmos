<!-- This file has been generated in whole or in part by a Large Language Model -->
# Self-hosting bootstrap

Kunix has an optional static musl development kit. It contains GCC 11.2.1,
GNU binutils 2.42, GNU make 4.4.1 and a snapshot of the Kunix source tree.
The kit is separate from the normal 128 MiB system root and updates.

On the development machine, prepare it with:

```sh
buildutils/selfhost-toolchain.sh
python3 buildutils/selfhost.py kit build/selfhost/selfhost.tar.gz
sha256sum build/selfhost/selfhost.tar.gz
```

The bootstrap script first uses its cache (`build/selfhost/cache`, overridden by
`SELFHOST_CACHE`), and uses the existing `~/src/binutils-2.42.tar.xz` when present.
Downloads have pinned SHA256 digests. The compiler is a binary bootstrap from
musl.cc; binutils and make are rebuilt from source against musl with static
linking and PIE disabled. Every staged ELF is checked for unsupported PIE,
an interpreter and dynamic library dependencies, including compiler subprocesses.

Transfer the archive over SSH to persistent `/home` after verifying its digest:

```sh
ssh root@KUNIX 'mkdir -p /home/root'
cat build/selfhost/selfhost.tar.gz | ssh root@KUNIX 'tar -xz -C /home/root'
```

Inside Kunix:

```sh
export SELFHOST_PREFIX=/home/root/selfhost/tools
export PATH="$SELFHOST_PREFIX/bin:$PATH"
cd /home/root/selfhost/src
make -f buildutils/selfhost.mk verify-source
make -f buildutils/selfhost.mk all
```

For a BIOS bootable ISO after compilation, run:

```sh
make -f buildutils/selfhost.mk image
```

This uses the static makefs and Limine installer in the kit, makes a new 128 MiB
UFS2 root with the newly compiled programs, and writes
`build/selfhost-image/kunix-selfhost.iso`. It retains the installed bootstrap
shell, BusyBox and X applications. It does not rebuild those upstream projects.

The kernel is `build/binaries/kunix-kodiak.elf`; native user programs are in
`build/user/*.elf`, Habilis in `build/habilis/habilis`, and Prime Meridian test
programs, network configuration and updater in `build/pm`. Keep these build
outputs on `/home` so normal root replacement does not discard the work.
The source manifest is verified **before** compilation; generated build files
subsequently change. The build ID is frozen when the bundle is created and
records the source revision and working tree status. Its manifest records the
actual bundled files, including local changes.

The kernel commands are extracted from the regular `build.sh` functions when
creating the bundle. They use the same source list and linker script. The
`x86_64-elf-*` wrappers use GCC's x86_64 freestanding support, kernel code model,
no PIE, a global stack canary and no red zone; the hosted compiler still targets musl for normal
Linux ABI applications. No Python or git is needed for these guest builds.

For an isolated development ISO with the kit already unpacked in a disposable
RAM root:

```sh
buildutils/selfhost-image.sh
python3 buildutils/test.py --iso build/selfhost/live.iso --no-ata \
    --mem 1536M --timeout 1800 --command selfhost-test \
    --log build/selfhost/guest.log
```

Build the ordinary system first so `build/ufsroot` and the kernel exist. This
optional ISO has a 512 MiB root and does not replace the normal root, disks,
installer image or update slots. `selfhost-test` compiles and executes a musl
program, then builds the complete kernel and native/Prime Meridian userland.
The development ISO's files disappear when its VM stops; use the persistent
archive workflow for actual work.

## Provenance and remaining work

`tools/share/licenses/selfhost/PROVENANCE` records bootstrap and source archive
URLs, hashes, GCC configuration and tool versions. The musl.cc compiler binary
is pinned, but this bootstrap does not rebuild GCC from its corresponding
source. Obtain and retain matching GCC sources before redistributing that binary
publicly. Binutils and GNU make source archives remain in the host cache.

This bootstrap is not the complete Phase 5 milestone. The full guest cycle now
passes under `python3 buildutils/selfhost_test.py --iso build/selfhost/live.iso`:
guest kernel, native userland and BIOS ISO builds, NVMe export with SHA256
match, and ktest/pmtest/pmsmoke/permtest/fbtest/xtest/vmtest on the
guest-built ISO. Rebuilding all upstream ported applications also
remains required. The optional static PIE binutils, C++ module mapper and header
fixer from the bootstrap archive are omitted. Two guest-assembly defects were
fixed during verification: the Kunix shell lacked the `exec` builtin every
Habilis service line needs (pinned by a new ktest check), and the guest image
now keeps BusyBox applet names like the host build instead of shadowing them
with untested native tools.

## Validation checkpoint at handoff

The reproducible toolchain bootstrap completed successfully, with static
ET_EXEC gcc, assembler, linker, GNU make, makefs and Limine. The source bundle
host tests passed. A real compiler regression test verifies the kernel uses
`__stack_chk_guard` instead of Linux TLS. Static makefs produced UFS2 and ISO9660
images with verified filesystem signatures.

A guest compiled, assembled, linked and executed a musl C program. It then
compiled many actual kernel sources. That run was deliberately stopped after
finding that the hosted compiler selected a TLS kernel canary. The wrappers now
force `-mstack-protector-guard=global`; a complete guest rerun and boot check are
still pending. `build/selfhost/guest.log` records the interrupted run, not a
successful full build. The earlier host-built kernel with the old canary did
not boot. **Regenerate any existing `selfhost.tar.gz` before using it:** the
old generated archive predates this fix.

To resume the mandatory end-to-end verification on the development machine:

```sh
# Toolchain bootstrap already completed; this is an offline repeat if needed:
SELFHOST_CACHE=/tmp/kunix-selfhost-cache buildutils/selfhost-toolchain.sh
python3 buildutils/tests/selfhost_bundle.py
python3 buildutils/tests/selfhost_kernel_flags.py
python3 buildutils/tests/selfhost_makefs.py
python3 buildutils/selfhost.py kit build/selfhost/selfhost.tar.gz
buildutils/selfhost-image.sh
python3 buildutils/selfhost_test.py --iso build/selfhost/live.iso
```

The final harness boots the development ISO, builds the full kernel and native
userland, builds a new UFS2 root and BIOS ISO inside Kunix, writes that ISO to
its own disposable QEMU NVMe disk, checks the exported SHA256 against the
guest's digest, then boots the result through the runtime suites. Its log is
`build/selfhost/guest-full.log`; its live log is
`build/selfhost/serial-full.log`. Successful export produces
`build/selfhost/kunix-guest-built.iso`. This TCG run can take tens of minutes.
No full guest compilation or exported image boot success is claimed at this
checkpoint. All generated binaries, archives and images are excluded from git.

This product includes software developed for the NetBSD Project by Wasabi
Systems, Inc. The makefs source notices and BSD queue header notice are retained
with the development kit licenses.
