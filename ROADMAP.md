<!-- This file has been generated in whole or in part by a Large Language Model -->

# Kunix roadmap and hand-off

This is for whoever picks up the work next, human or agent (Codex, Claude, ...). It covers where Kunix stands on 2026-10-16, how to build and test it, the traps that have already cost time, and the plan, in order, with what "done" means for each step.

Read this first. Then read `LARGELANGUAGEMODELS.TXT` and `HabilisPlan.txt`.

---

## 0. Ground rules

- **Policy.** The Millennium Decree in `LARGELANGUAGEMODELS.TXT` is the policy in force. LLM-written code must integrate into Kunix with little reconfiguration: match the surrounding style, extend what exists, and don't invent parallel frameworks.
- **File header.** Any file that is more than half LLM-written starts with the line `This file has been generated in whole or in part by a Large Language Model`, as a comment in that file's syntax.
- **Commits.** End each commit message with a `Co-Authored-By:` line naming the model that did the work.
- **The owner.** Joseph Jones (git user `jdj`) owns the project. Their files carry their own comments and voice, so edit them conservatively.
  - `kernel/console.c` and `drivers/keyboard.c` are the owner's.
  - `lib/memcpy.c` and `lib/memset.c` are also the owner's.
- **Branch.** Work happens on `prime-meridian`. `main` is the default branch. Commit often, with messages that explain why.
- **Tests.** Never commit with failing suites (section 2). Add tests for anything new.
- **Comments.** The codebase explains *why* in plain English and avoids jargon-heavy shorthand. Keep it that way.

## 1. What Kunix is

Kunix is a hobby x86_64 kernel (codename Kodiak) with compatibility layers:

- **Kunix has its own native syscall ABI.** Compatibility personalities translate their ABIs onto shared kernel services.
- **Prime Meridian** is the Linux ABI. It runs unmodified static Linux/musl binaries.
- **Inferno** (planned) is a BSD ABI.
- **Habilis** is a kernel-agnostic init system.

**The end goal is a desktop OS.** A possible first niche is a "1990s UNIX workstation done properly": X, Motif or CDE, a small system you can understand top to bottom. The owner hasn't committed to that niche yet.

**First real hardware target: a Lenovo ThinkPad T14 Gen 1 (AMD Ryzen 5 PRO 4650U, 16 GB, 256 GB NVMe).** Windows on it may be wiped. The live USB image boots it and "worked great" (2026-10-16). The development machine is a different computer.

**Ideas the owner has asked to keep for later (not yet):**
- An NT-style text installer offering "breeds":
  - **Poodle**: pure Kunix, with an eventual native `klibc` and `kutils`.
  - **Bernard**: the default. Kunix where possible, Linux fallbacks for drivers, musl.
  - **Newfoundland**: the Kunix kernel with a full GNU/Linux userland.
  - A fourth breed is unnamed.
- A **Kunix analyzer** that shows how much of an installed system is Kunix, Linux or other.
- Eventually NVIDIA drivers.
- A package manager named **`arf`** with **`.pkg`** files.

### What works today

- **Kernel basics:**
  - Limine boot (BIOS and UEFI), PMM/VMM, preemptive scheduler.
  - Processes with fork (copy-on-write), exec (ELF and `#!` scripts) and signals.
  - Sessions and process groups.
  - Users and permissions: uid/gid, set-uid exec, `AT_SECURE`.
- **Filesystems:**
  - VFS with symlinks and hard links.
  - UFS2 read-write and fsck-clean; it is the root. FAT32 at `/fat`.
  - tmpfs at `/tmp` (hard links, sockets).
  - procfs at `/proc`, devfs at `/dev`.
- **Terminals:**
  - A termios tty layer and a VT100/ANSI emulator on the framebuffer console (TERM=linux; fast scrolling through a ring buffer).
  - Unix98 ptys.
- **IPC:** pipes, Unix domain sockets (`SCM_RIGHTS`, `SO_PEERCRED`, abstract names), `poll`/`select`.
- **Memory:** mmap, munmap and mprotect (anonymous, demand-paged private files/ELF, MAP_SHARED anonymous, device memory).
- **Prime Meridian:** about 150 Linux syscalls; static musl programs run.
  - zsh 5.9.2 is the login shell; busybox provides everything else.
- **Habilis:** `/sbin/init` with services in `/etc/habilis/*.service` and busybox `login` on the console.
- **Graphics and input:**
  - `/dev/fb0` with Linux fbdev ioctls and mmap.
  - The PS/2 keyboard (raw and keycode modes) and `/dev/input/mice` (PS/2 mouse).
  - KD/VT ioctls; the console hands the screen to X and takes it back.
- **X11:** xorg-server 1.19.7 Xfbdev, twm and xterm, all static against musl. `startx` works on QEMU and on the T14.
- **Live image:** `build/binaries/kunix-live.iso` boots any BIOS/UEFI PC with the root filesystem in memory.

### Missing (the big ones)

| Missing | What it blocks |
|---|---|
| Networking | SSH, packages, CDE, Claude Code |
| A disk/page cache | Performance |
| Demand paging of executables | Binaries are read whole at exec |
| Shared libraries | Large software stacks |
| Threads | `clone(CLONE_THREAD)` returns ENOSYS |
| epoll, eventfd, timerfd | Modern event loops |
| NVMe and AHCI drivers | Installing to real disks |
| USB | External keyboards, mice and sticks |
| Real ACPI | Power-off uses QEMU's port |
| SMP | Single core only |

---

## 2. Build and test

**Toolchain.** The cross-compiler (`x86_64-elf-gcc` 13.2) is in `/usr/local/cross/bin`.

**`./buildutils/build.sh`** does everything:
1. disk
2. kernel compile
3. link
4. userland
5. Prime Meridian tests
6. ufs root image
7. home image
8. ISOs
9. launches QEMU interactively

For automated work, use a copy without the QEMU launch:

```
sed 's/^    qemu$/    :/' buildutils/build.sh > ~/.cache/build_noqemu.sh
bash ~/.cache/build_noqemu.sh > ~/.cache/build.log 2>&1
grep -E " error|warning:" ~/.cache/build.log | grep -v RWX
```

**Regenerate that copy after every edit to build.sh**, or it silently runs old steps. **build.sh does not stop on compile errors**: always grep the log, or you'll test a stale kernel.

**Test suites.** `python3 buildutils/test.py --all` boots a headless QEMU per suite and takes about 15 minutes, so run it in the background. `--suite NAME` runs one suite.

| Suite | What it tests | Checks |
|---|---|---|
| `ktest` | Native syscalls | 109 |
| `pmtest` | Linux ABI via musl | 103 |
| `pmsmoke` | zsh and busybox, as a person uses them | 46 |
| `permtest` | Users and permissions | 15 |
| `fbtest` | Framebuffer, mouse, raw keyboard | 27 |
| `xtest` | X end to end | 9 |
| `vmtest` | Lazy file/ELF mappings, COW, backing lifetime, EOF and failed exec | 17 |
| `ufs` | Writes, reboots, verifies, then runs host `ufsdiag.py` and FreeBSD `fsck_ffs` | 43 + 9 |

Host sanitizer tests run with `python3 buildutils/test_host.py`. They exercise
production block/page cache code, fault filling (including no-cache first writes
and overlapping ELF BSS), battery normalization, logs and ACPI parsing.

For a battery fixture, generate an SSDT and pass it to QEMU through the harness:

```
python3 buildutils/tests/battery_table.py /tmp/kunix-battery.aml
python3 buildutils/test.py --command powertest --acpi-table /tmp/kunix-battery.aml
python3 buildutils/tests/battery_table.py /tmp/kunix-battery-low.aml --low
python3 buildutils/test.py --command 'powertest low' --acpi-table /tmp/kunix-battery-low.aml
```

These fixtures test the interpreter and reporting, not the laptop's EC. ACPI
reset tests use `--machine q35`: the default `pc` firmware lacks a reset register
and therefore uses the chipset fallback.

How the harness works:
- Each suite logs in as root, types the suite's name at the prompt, and reads `PASS`/`FAIL` lines and a `NAME: N passed, M failed` summary from `/dev/serial`.
- A test can ask for machine input by printing `monitor: <qemu monitor command>` to `/dev/serial`, for example `monitor: sendkey a` or `monitor: mouse_move 5 5`.
- Tests live in:
  - `user/ktest.c`
  - `compat/primemeridian/tests/*.c` and `*.zsh`. These are installed to `/bin` under their base names.

**Disks in QEMU:**

| Index | Image | Where | Notes |
|---|---|---|---|
| 0 | `disk.img` | `/fat` | FAT32, tracked in git, dirtied by every build |
| 1 | `ufs.img` | `/` | UFS2, rebuilt every build, 128 MiB |
| 3 | `home.img` | `/home` | UFS2, created once, untracked, survives rebuilds |

**Toolchains outside the tree** (everything lands in `~/opt/primemeridian`):
- `buildutils/pm-toolchain.sh {musl|ncurses|zsh|busybox}` builds musl 1.2.5, ncurses (with built-in linux, vt100, dumb and xterm entries), zsh and busybox.
- `buildutils/x11.sh` builds about 24 X packages into `~/opt/primemeridian/x11`.
  - Each package leaves a stamp and a log in `x11/.built/`. Delete a stamp to rebuild that package.
  - It also sed-patches several xorg-server 1.19.7 bugs; each patch is commented with its reason.
- `buildutils/fsck-ffs.sh` builds FreeBSD's `fsck_ffs` for the host.

**Debugging techniques that worked:**
- **Syscall tracing.** Temporarily log a chosen process's syscalls at the end of `pm_dispatch` (`compat/primemeridian/syscall.c`): `kserialoutf` with nr, args and result, filtered by `p->name`, capped by count. Remove it before committing.
- **Finding a spin.** Sample a process's user RIP from the timer IRQ (`kernel/idt.c`) and look the addresses up in the unstripped binary.
- **Screenshots.** Drive QEMU through its monitor socket: `sendkey`, `mouse_move`, `screendump x.ppm`.
- **Unknown syscalls.** Unimplemented Linux syscalls are logged once each to serial: `primemeridian: NAME called unimplemented syscall N`.

### Traps already hit (don't repeat them)

- **`bool` is `typedef u8 bool`.** `bool b = flags & 0x100` truncates to false. Write `(flags & X) != 0`.
- **The auxv must end with an explicit `AT_NULL`.** There's a static assert for it.
- **musl sign-extends ioctl request numbers.** Mask them with `(u32)req`.
- **bash ignores `set -e`** inside anything tested with `||`, such as `( fn ) || ...`, so failures sail on. See the `build()` function in `x11.sh`.
- **`tar -tf x | head -1` under `pipefail`** kills the script silently through SIGPIPE. Use `awk 'NR==1'`.
- **Unpack sources fresh.** Unpacked over an old tree, make finds stale generated files up to date.
- **libtool links programs dynamically** unless given `-all-static` at make time.
- **Build-time helper programs** need `CC_FOR_BUILD=gcc`.
- **musl doesn't define `_POSIX_SOURCE` the way glibc does.** Old code that tests for it (xterm) takes wrong branches.
- **Don't edit a bash script while it's running.** bash reads scripts as it goes.
- **Hardware has no IDE.** Probing a missing IDE controller reads 0xFF. Any ATA wait loop without a check would spin forever on modern hardware.
- **zsh doesn't word-split unquoted variables.** Run build loops that rely on splitting under `bash -c`.
- **Don't `pkill -f` patterns** that match your own shell.

---

## 3. Map of the tree

| Area | Files |
|---|---|
| Boot, init order, root mount order (ramdisk module, then ATA slave, then FAT32) | `kernel/kernel.c` |
| PMM (refcounts; device memory has refcount 0 and is never freed), VMM, copy-on-write | `kernel/memory.c` |
| mmap and VMAs | `kernel/mman.c`, `include/mman.h` |
| Processes, scheduler, signals | `kernel/proc.c`, `kernel/signal.c` |
| ELF loading, user stack, auxv, `#!` | `kernel/usermode.c`, `kernel/elf.c` |
| Native syscalls (`int 0x80`) | `kernel/syscall.c` |
| Linux ABI: syscall table, signal frames, sockets | `compat/primemeridian/syscall.c`, `signal.c`, `socket.c` |
| Linux ABI: fbdev, KD and VT ioctls | `compat/primemeridian/display.c` |
| Linux constants | `compat/primemeridian/linux.h` |
| Files, pipes, poll | `kernel/file.c` |
| Unix sockets | `kernel/socket.c` |
| VFS (attributes, permissions, ops table incl. `mmap` for devices) | `fs/vfs.c`, `include/vfs.h` |
| Filesystems | `fs/ufs2.c`, `fs/fat32.c`, `fs/tmpfs.c` (nodes plus directory entries), `fs/procfs.c`, `fs/devfs.c` |
| Disk layer: disk numbers 0–3 ATA, 4–7 ramdisks from Limine modules | `drivers/disk.c`, `include/disk.h` |
| Drivers | `drivers/ata.c`, `keyboard.c`, `mouse.c`, `fb.c`, `tty.c`, `pty.c`, `vt.c`, `pci.c`, `pit.c`, `rtc.c`, `serial.c` |
| Console: shadow buffer as a ring of text rows, flushed at most about 60 times a second; graphics mode | `kernel/console.c` |
| TSC time | `kernel/time.c` |
| Power: firmware ACPI S5/reset, with platform fallbacks | `kernel/power.c`, `kernel/acpi.c` |
| Battery: AML interpreter, EC and cached power status | `kernel/battery.c`, `kernel/battery_acpi.c`, `kernel/uacpi_host.c`, `vendor/uacpi/` |
| Clean file page cache | `kernel/pagecache.c` |
| Init | `habilis/habilis.c`, `etc/habilis/*.service` |
| Files copied into `/etc` | `etc/` |
| `startx` | `x11/startx` |

---

## 4. The roadmap

The phases follow the owner's chosen order: T14 basics, disk cache and demand paging, NVMe and the installer, networking and SSH, self-hosting, desktop, USB, threads and supporting kernel capabilities, Claude Code/Codex, and finally arf. Each lists what "done" means. Sizes are rough, in focused working sessions.

### Phase 1: T14 hardware basics (about 1 session)

The live image boots the T14. Make it pleasant and debuggable there.

**Implemented and verified in QEMU (2026-10-16); T14 checks pending:** the log ring, `syslog` reads,
`/proc/kmsg`, panic log display, ACPI S5/reset, consistent framebuffer cache
attributes and dynamically allocated console storage are implemented. All seven
regression suites pass, including a clean `fsck_ffs` result. BIOS and UEFI ACPI
shutdown/reset checks pass; framebuffer and X checks pass at 1920×1080 and
2560×1440, with an additional UEFI framebuffer check at 1920×1080. Host sanitizer
checks pass (`python3 buildutils/test_host.py`), and the panic display was checked
visually. The updated live image still needs testing on the T14; laptop power
controls and X performance are not yet verified.

1. **Kernel log you can read without serial.**
   - Keep a ring buffer (64 KiB or so) of everything `klog` and `kserialout` print.
   - Expose it as `/proc/kmsg` and through Linux `syslog(2)` (`SYSLOG_ACTION_READ_ALL`, nr 103), so busybox `dmesg` works.
   - Show panics on screen with the last log lines.
2. **Real ACPI power-off and reset.**
   - Request `LIMINE_RSDP_REQUEST`; walk RSDT/XSDT to the FADT.
   - Power-off: write `SLP_TYPa | SLP_EN` to `PM1a_CNT` (and PM1b if present). The `SLP_TYPa` value comes from the `\_S5_` package in the DSDT; a byte scan for `_S5_` followed by the package encoding is the usual minimal approach (no full AML interpreter).
   - Reset: the FADT reset register; fall back to port 0xCF9, then the keyboard controller.
   - Keep QEMU's 0x604 path working.
3. **Write-combining framebuffer.**
   - Limine already configures PAT entry 5 as WC and uses it for the kernel's framebuffer mapping.
   - Preserve those cache attributes in user `/dev/fb0` mappings, including across fork and `mprotect`, so both mappings use the same memory type.
   - Check X performance on the T14 at 1920×1080.
4. **Larger screens.**
   - Allocate the console shadow buffer from the actual mode, and reject unsupported or unallocatable modes cleanly.
   - Test QEMU at 1920×1080: Limine's `resolution:` option or `-device VGA,xres=...`.
5. **Battery and AC status.**
   - Read live charge level, charging/discharging/full state and AC adapter status through the firmware's ACPI battery methods (`_BIX`/`_BIF`, `_BST`, `_PSR`) and embedded controller.
   - Expose the information through shared kernel services and a native Kunix `battery` command, accessible alongside the compatibility ABIs.
   - Report missing or unknown readings honestly, and warn when a discharging battery is low.
   - This needs AML evaluation and EC access beyond the literal S5 package parser. Test real readings on the T14.
   - Implemented with uACPI 7.0.0, bounded polling EC I/O, 30-second samples,
     `/proc/battery` and native `/bin/battery`. The interpreter runs in PID 0;
     SCI work is polled with its PIC line masked. Firmware fixtures verify
     discovery, charge, units, AC state and low warnings. Real T14 readings
     and EC behavior are still pending; this is not suspend/resume support.
6. **APIC (if the PIC misbehaves on hardware, and for SMP later).**
   - Parse the MADT; enable the local APIC and the IO-APIC.
   - Route IRQ 1, 12 and the timer.
   - Calibrate the local APIC timer against the PIT or HPET.
   - Do this when there's a reason; the T14 currently works with the PIC.

**Done when:**
- `dmesg` works.
- `poweroff` turns off both QEMU and the T14, and `reboot` restarts both.
- X is noticeably faster on the T14.
- Battery charge/charging/AC status updates on the T14, and low-battery warnings work.
- All suites pass.

### Phase 2: a disk cache and demand paging (about 1–2 sessions)

**Implemented (2026-10-16):** RAM-sized 4 KiB LRU write-back cache (up to
32 MiB), deferred five-second flushing, native `sync`/`fsync` and Linux
sync calls, and shutdown ordering that flushes UFS data before marking it
clean. FAT32 now uses the same disk layer. `/proc/cache` exposes statistics.
A bounded clean file cache (up to 16 MiB) backs private file mappings and
ELF segments; faults load only touched pages, preserve COW and backing
lifetimes, zero partial pages/BSS, and report inaccessible file pages as
SIGBUS. Exec retains the old image on failure and refuses writes to running
executables. Limits are now 1 GiB resident pages and a 512 MiB ELF image.

A controlled BIOS/PC QEMU comparison (256 MiB RAM, identical userland and
initial disk images) measured console startup at **7.612 → 6.210 seconds**
and X plus xterm readiness at **2.300 → 1.286 seconds**. X is timed between
serial markers on the host at 10 ms polling intervals: guest PIT-based uptime
misses time during long IRQ-off disk operations and gave misleading results.
These are single-run QEMU measurements; T14 performance remains unmeasured.

All eight suites pass (378 checks), including UFS write/reboot verification and
a clean `fsck_ffs` result. Host sanitizer checks pass. Native/Linux sync calls,
cache failure retry, mapped-file lifetime, private COW, sparse large mappings,
192 MiB ELF BSS residency, EOF SIGBUS and exec rollback have regression tests.

1. **Block cache in the disk layer.**
   - 4 KiB blocks, LRU, write-back with a periodic flush and flush on sync/shutdown, sized from free RAM.
   - UFS2 already funnels all I/O through `disk_read`/`disk_write` (`fs/ufs2.c` `dev_read`/`dev_write`).
2. **A page cache for regular files** (for mmap and exec), at least read-only.
3. **Demand-paged ELF loading.**
   - Map segments as file-backed VMAs and fault pages in.
   - Today `elf_load` reads whole segments. Xfbdev (3 MB) and later 100 MB binaries need this.
4. **Raise per-process limits.** `USER_MAX_PAGES` is about 128 MiB today.

**Done when:**
- Booting and `startx` are measurably faster; time them.
- ufs and fsck stay clean.
- A large file-backed `mmap` doesn't copy the whole file up front.

### Phase 3: NVMe, GPT and installing to the T14 (about 2–3 sessions)

1. **NVMe driver.**
   - PCI class 01:08:02: admin queue, identify, one I/O queue pair, polled completions first, then MSI or legacy IRQ.
   - QEMU has `-device nvme`, so develop there.
2. **GPT parsing in the disk layer.** Partitions become disk numbers; add an `include/disk.h` naming scheme.
3. **Mount the root from a GPT partition** (by partition GUID or type).
   - The kernel command line comes from Limine (`kernel_cmdline`), e.g. `root=PARTUUID=...`.
4. **The installer.** The owner wants an NT-style text installer (blue screen, white text, F-keys).
   - Simplest correct design:
     1. Partition the NVMe as GPT: a 512 MiB EFI system partition (FAT32), a Kunix root (UFS2), and optionally a separate home (UFS2).
     2. Format the ESP with busybox `mkdosfs`.
     3. Copy Limine's `BOOTX64.EFI` and `limine.conf`, and the kernel, onto it.
     4. Write the root by copying the live `root.img` onto the partition, then growing it. Writing a UFS2 `newfs`/`growfs` is a sub-task; FreeBSD's sources are a reference. `ufschown.py` and `ufsdiag.py` show the on-disk structures.
   - The installer is the natural place for the "breeds" choice later; for now, just Bernard.
5. **Write safety.** Before writing to a real disk, show the plan and require typing `YES`.

**Done when:** the T14 boots Kunix from its NVMe with no USB stick, and a persistent `/home` survives reboots.

### Phase 4: networking (about 3–4 sessions; the biggest single item)

**Decision for the owner:** write our own TCP/IP stack (fits the "Poodle"/pure-Kunix idea; 2–3 sessions) or port lwIP (BSD-licensed; about 1–1.5 sessions). Ask before starting.

1. **e1000 driver** (QEMU's `build.sh` already attaches `-device e1000`; PCI 8086:100e), for development.
2. **The stack:**
   - Ethernet, ARP, IPv4, ICMP (ping), UDP, TCP (correct state machine, retransmission, windowing).
   - Loopback (127.0.0.1).
   - A DHCP client (kernel or a Habilis service using busybox `udhcpc`, which needs `AF_PACKET` or raw sockets; a kernel client is simpler).
3. **Prime Meridian `AF_INET` sockets** (and `AF_INET6` returning EAFNOSUPPORT cleanly):
   - `socket`, `bind`, `connect`, `listen`, `accept4`, `sendto`, `recvfrom`, `sendmsg`, `recvmsg`, `getsockname`, `getpeername`, `setsockopt` (`SO_REUSEADDR`, `TCP_NODELAY`, `SO_RCVTIMEO`...), `shutdown`, `poll` readiness.
   - Reuse the shape of `kernel/socket.c` and `compat/primemeridian/socket.c`.
4. **Network configuration.** Basic `SIOCGIFCONF`, `SIOCGIFADDR` and `SIOCGIFFLAGS` ioctls so busybox `ifconfig` and `ip` show something. `/etc/resolv.conf` written by DHCP; musl's resolver then works over UDP.
5. **RTL8111/8168 driver (r8169)** for the T14's Ethernet port. Confirm the chip with `lspci -nn` from Linux on the T14. QEMU doesn't emulate it, so test on the laptop.
6. **Dropbear SSH** (client and server), static against musl, added to `pm-toolchain.sh`. It needs `/dev/urandom` or `getrandom` (present), ptys (present) and shadow passwords (present).

**Done when:**
- `ping`, `wget http://...` and `ssh user@host` work from Kunix in QEMU and on the T14.
- `ssh root@kunix` works from another machine.
- New `nettest` suite passes.

### Phase 5: self-hosting

Build Kunix from inside Kunix through Prime Meridian.

1. **Development tools.** Bring up a musl-hosted C compiler, assembler, linker and `make`, plus the shell and utilities needed by the build scripts.
2. **Kernel build.** Provide an `x86_64-elf` toolchain that runs on Kunix and adapt the build scripts to use it.
3. **Image build.** Port the remaining build dependencies needed to produce the userland, filesystem images and bootable ISOs on Kunix.
4. **Build workflow.** Use persistent storage from Phase 3 and networking/SSH from Phase 4 to keep sources, fetch dependencies and transfer builds.

**Done when:** Kunix can build its kernel, userland and bootable image from source, and the resulting image boots and passes the test suites when tested from the development machine.

### Phase 6: desktop (each item about 0.5–1 session unless noted)

1. **JWM** (plain C, Xlib only): a taskbar, menu and tray. Add it to `x11.sh` and make it the default in `startx`.
2. **XDM** (needs libXdmcp): a graphical login as a Habilis service replacing the console `login`.
   - Interim option: start X from `/etc/zprofile` when logging in on the console.
3. **Mouse wheel:**
   - Emulate Linux mousedev's ImPS/2 (sample-rate sequence 200,100,80 gives ID 3 and 4-byte packets) in `drivers/mouse.c`, and enable the real mouse's wheel mode at init.
   - `startx` then passes `protocol=imps/2`.
4. **Fonts and polish:** more misc and Unicode fonts; a nicer default `system.twmrc` or JWM config.
5. **Motif (2.3.8) and mwm.** Nearly the CDE look; needs libXft and freetype (optional).
6. **CDE** (several sessions). Needs:
   - networking (ToolTalk uses Sun RPC plus rpcbind on loopback)
   - a C++ compiler for musl (build musl-cross-make's gcc/g++)
   - realistically, shared libraries (Phase 8.3); revisit CDE after that support lands
   - probably glibc-isms fixed
7. **Wi-Fi** (much later; the T14 has Realtek or Intel Wi-Fi). Hard: firmware loading, 802.11, WPA.

**xorg-server 1.19.7 notes (already handled in `x11.sh`; keep in mind):**
- Xfbdev takes input only from `-keybd keyboard -mouse mouse,,protocol=ps/2`.
- Its keyboard uses XFree86 AT codes, hence `--with-default-xkb-rules=base`.
- The default root cursor is invisible.
- kdrive rejects mouse jumps over 50 px until the protocol is "proven" (patched to skip when a protocol is given).

### Phase 7: USB (about 2–3 sessions)

xHCI host controller driver, then HID (keyboard and mouse, boot protocol first), then mass storage (BOT, then UAS) through the disk layer. The T14's USB-C and USB-A ports and the docking story depend on this. The internal keyboard and TrackPoint are PS/2 and already work.

### Phase 8: threads and supporting kernel capabilities (several sessions)

1. **Threads:**
   - `clone(CLONE_VM|CLONE_THREAD|CLONE_SIGHAND|CLONE_SETTLS|CLONE_PARENT_SETTID|CLONE_CHILD_CLEARTID)`.
   - Per-thread FS base and signal mask; thread groups (getpid versus gettid).
   - Futex wait/wake must really block (`futex` exists; check its semantics).
   - `exit` versus `exit_group`.
   - Tests: musl `pthread_create`/join/mutex/cond in a new pmtest section.
2. **epoll, eventfd, timerfd, signalfd** (Node, Bun and most modern servers use them).
3. **Shared libraries:**
   - `PT_INTERP` support in the ELF loader, so musl's dynamic linker (`/lib/ld-musl-x86_64.so.1`) runs.
   - Correct file-backed `MAP_PRIVATE` mappings with copy-on-write.
   - `mprotect` on them. Most of the remaining mmap work is here.
4. **SMP:** after the APIC, bring up the other cores (locking audit first; the kernel assumes one CPU and uses `irq_save` as its lock).
5. **Real timer and clock work:** HPET or APIC timer, tickless idle (battery life on the T14).

### Phase 9: Claude Code and Codex on Kunix (after phases 2, 4 and 8)

- Run Claude Code and the Codex CLI **through Prime Meridian**.
- Check the available Linux builds, musl compatibility and runtime requirements for each tool when this phase begins.
- Check whether each binary is static or dynamic. Dynamic binaries require Phase 8.3.
- Check each tool's needs for:
  - TCP plus TLS (verify how each tool supplies TLS)
  - DNS
  - threads
  - epoll, eventfd, timerfd
  - a large address space
  - `git` and `ripgrep` (port them)
- The console's 8×16 font is CP437-only, so run the tools in xterm with a Unicode font (or add UTF-8 to `drivers/vt.c`).

**Done when:** Claude Code and the Codex CLI can authenticate and complete a coding task from Kunix.

### Phase 10: arf, the package manager (about 1–2 sessions)

Owner's choice: the name is **arf**, packages are **`.pkg`** files.

- **Format:** a `.pkg` is a compressed tar (gzip now, zstd later) containing the files plus `+MANIFEST`:
  - name, version, architecture, dependencies
  - the file list with SHA-256 hashes and modes
  - `origin`: `kunix` | `linux` | `gnu` | ... This "breed" tag feeds the Kunix analyzer.
- **Database:** `/var/db/arf/` holds installed manifests.
- **Commands:** `arf install FILE|NAME`, `arf remove`, `arf list`, `arf info`, `arf verify`, `arf files`, `arf owner PATH`.
- **Signing from day one:** an ed25519-signed repository index (`index.sig`), so plain HTTP mirrors are safe before arf speaks TLS.
- **Written in C** against musl (moves to klibc later). Small.
- **Build integration:** build.sh packages zsh, busybox and X as `.pkg` files and installs them into the root image with arf, so the image is "base plus packages".
- **Hosting:** `arf fetch` and `arf update`. Hosting is static files: an index plus `.pkg` files on any web server or GitHub Releases.

**Done when:** the root image is built from packages, `arf verify` passes, and later `arf install jwm` downloads, checks and installs.

### Longer-term ideas: breeds, installer polish and the analyzer

- **Breeds in the NT-style installer:** Poodle, Bernard (default), Newfoundland, and a fourth to be named.
- **klibc and kutils:** a native libc and utilities for Poodle.
- **The Kunix analyzer:** reads arf's origin tags plus the kernel's own driver list and reports the Kunix, Linux and other share per area (kernel, drivers, libc, userland).
- Prepare for this now: keep each binary's origin traceable in build.sh (native `user/`, `pm-toolchain.sh`, `x11.sh`).

---

## 5. Open decisions (ask the owner)

1. Write our own TCP/IP stack or port lwIP? (Phase 4)
2. Commit to the "1990s UNIX workstation" niche (Motif/CDE), or stay general?
3. The fourth breed's name and purpose.
4. Whether `home` gets its own partition on the T14.
5. When to start hosting packages, and where (GitHub Releases is the easy start).

## 6. T14 Gen 1 AMD facts (verify with `lspci -nn` from Linux on the laptop)

| Part | Details |
|---|---|
| Firmware | UEFI; Secure Boot must be off (Limine is unsigned). F1 opens setup, F12 the boot menu. |
| Display | Radeon (Renoir); the GOP framebuffer from Limine works. Probably 1920×1080. |
| Keyboard, TrackPoint | PS/2 through the embedded controller: working. The touchpad likely works as a PS/2 mouse. |
| Disk | NVMe, 256 GB (Windows may be erased). |
| Ethernet | Very likely Realtek RTL8111 (r8169). Wi-Fi is Realtek or Intel. |
| Debugging | No serial port: debug with the screen (photos) and, after Phase 1, `dmesg`. |
| Write the live USB | `sudo dd if=build/binaries/kunix-live.iso of=/dev/sdX bs=4M status=progress conv=fsync` |
