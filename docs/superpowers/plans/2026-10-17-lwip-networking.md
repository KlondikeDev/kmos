<!-- This file has been generated in whole or in part by a Large Language Model -->
# lwIP Networking Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans; independent driver,
> core and ABI tasks use superpowers:dispatching-parallel-agents.

**Goal:** Bring up lwIP IPv4 networking and SSH through Kunix-owned socket services.
**Architecture:** NO_SYS raw API, IRQ serialization, bounded polled e1000,
Kunix socket file objects, independent native/Linux translation.
**Tech Stack:** C, lwIP 2.2.1, static musl, Dropbear, QEMU user networking.
**Spec:** docs/superpowers/specs/2026-10-17-lwip-networking-design.md

## Global Constraints
Preserve native ABI independence and existing Unix sockets; no automatic SSH
listener. Use only disposable VM images and localhost test servers. Keep vendor
license/provenance. Preserve pre-existing generated-file modifications.

## Review Focus
- Close/error while callbacks own sockets: no freed callback context or PCB access.
- Malformed/oversize input: bounded DMA lengths, pbuf queues and user copy sizes.
- CPU-bound guest: timers/RX continue; never enter lwIP from IRQ or concurrent contexts.
- Nonblocking connect, half-close and poll: distinguish pending, error, EOF and writable.
- Authentication entropy: do not expose weak-random host keys as secure SSH.

### Task 1: e1000 Ethernet driver
Files: drivers/e1000.c, include/e1000.h, buildutils/tests/e1000*.c.
Consumes PCI/PMM/VMM/time services. Produces exact include/e1000.h interface.
- [ ] Test TX/RX descriptor ownership, wrap, truncation, reset deadlines and invalid frames RED.
- [ ] Implement 82540EM (8086:100e), permanent bounded DMA rings, MAC, link and polled I/O.
- [ ] Run host tests and cross compilation GREEN; report limitations.

### Task 2: lwIP port and shared IPv4 sockets
Files: kernel/inet.c, include/inet.h, include/lwip-port/**, port host tests.
Consumes e1000 interface. Produces every service declared in include/inet.h.
- [ ] Test loopback UDP/TCP, bounded queues, errors, close, shutdown and readiness RED.
- [ ] Implement freestanding headers/memory hooks, NO_SYS init/poll, DHCP and Kunix socket callbacks.
- [ ] Verify service tests/cross compilation GREEN with no scheduler calls in callbacks.

### Task 3: Linux ABI and guest network tests
Files: compat/primemeridian/inet.c, compat/primemeridian/tests/nettest.c.
Consumes include/inet.h. Produces bool pm_inet_call(nr,a,b,c,d,e,f,i64*result),
return false for non-internet descriptors so existing Unix socket dispatch runs.
- [ ] Guest tests for IPv4, loopback, TCP/UDP, nonblocking, timeout, bad pointers and IPv6 RED.
- [ ] Translate sockaddr_in, options, sendmsg/recvmsg, accept4 and names; preserve Unix behavior.
- [ ] Compile and report QEMU cases for parent integration.

### Task 4: boot, build, maintenance, native ABI and network configuration
Files: buildutils/build.sh/test.py, kernel/kernel.c/proc.c/syscall.c,
compat/primemeridian/syscall.c, include/syscall.h, user/kunix.h, fs/procfs.c.
- [ ] Integrate above objects, vendor source list and driver, net_init/net_poll.
- [ ] Add independent native socket calls and native netinfo test, /proc/netinfo,
  interface ioctls and DHCP-derived resolver configuration.
- [ ] QEMU tests for DHCP, host-local TCP/UDP/DNS and inbound port forwards GREEN.

### Task 5: SSH and complete verification
Files: buildutils/pm-toolchain.sh, SSH integration harness, roadmap.
- [ ] Audit getrandom entropy; port static Dropbear without automatic listener.
- [ ] Verify SSH commands and terminal operation in disposable VMs.
- [ ] Run all regressions/host tests; independent full review; fix findings and commit.

## Execution ledger
Baseline: acd97f6, 8/8 suites (378 checks) and host tests passed in the previous
turn. Work in place on prime-meridian by established preference. Initial dirty
disk.img/ELF backed up in build/phase4-start. Shared interface preflight: driver
and core consume e1000.h; core and both ABI layers consume inet.h; all network
addresses use network byte order. Planning questions remain waived by owner.


### Integration ledger (in progress)

- Task 1 host tests: ASan/UBSan descriptor ownership, wrap, reset deadline,
  malformed RX and bounds passed; freestanding -Werror passed.
- Task 2 real-stack host tests: loopback TCP/UDP, bounds, close/half-close,
  errors, timeouts/signals and TSC wrap passed. TSC deadlines replace lost PIT
  ticks; scheduler sleeps remain bounded relative PIT deadlines.
- Task 3 adapter host arena tests: 104 checks pass; callbacks see kernel-only
  pointers, receive/accept validate output ranges before consuming data.
- Baseline guest RED: IPv4 nettest 1 pass/2 failures; native ABI 0 pass/4 failures.
- First integrated guest got DHCP DNS 10.0.2.3 and IPv4 sockets, then reproduced
  a NULL-vnode fstat panic. Native/Linux socket metadata and fstatfs handling
  fixed. Subsequent guest has 48 loopback/ABI/interface checks passing.
- First network matrix fails five external cases; separate SSH guest successfully
  fetches two credential files over HTTP, then Dropbear exits1 at startup. Logs
  are /tmp/kunix-phase4-network1.log and build/phase4/ssh-test/serial-live.log.
  Detailed per-operation errno and bounded Dropbear log dumps added; next run
  /tmp/kunix-phase4-{network,ssh}2.log in progress. No guessed kernel fix applied.
- First all-suite run 7/8 passed, UFS fsck clean. Only failure is stale pmtest
  assertion that IPv4 must be unsupported. Updated assertion to expect a socket
  and close it; full regression rerun remains required.
- Entropy host stub tests and real CPU tests pass. getrandom and random devices
  now use hardware only, bounded reads/retries and explicit EAGAIN when absent;
  no weak authentication fallback. QEMU harness uses CPU max and no implicit NIC.
- Dropbear2026.94 static tools built in build/phase4-toolchain; no autostart.
  Test credentials are generated per run, staged only in live RAM roots, removed
  on host completion, and never copied into build/ufsroot or installer payload.
- Build5 passes with only existing linker RWX warning. Fresh independent review
  agent network_review is running; possible small TCP receive-buffer deadlock
  under investigation. Source not committed yet.


### Review fixes and diagnosed integration failures

- External failures were early DHCP readiness: DNS option available while ACD
  still leaves IPv4 0.0.0.0. Explicit bounded readiness check added; subsequent
  run passes DHCP, TCP/UDP echo, DNS and busybox HTTP payload verification.
- Dropbear was built without syslog, so test's -E option is unsupported. Removed
  it. Authentication then correctly rejected root's /bin/zsh because /etc/shells
  was absent; added the actual installed /bin/sh and /bin/zsh (no auth bypass).
- Reviewer P1: vnode-less socket file operations reproduced a QEMU PAGE FAULT
  after all network checks passed (network3.log). Guarded lseek, directory calls,
  positional I/O, sendfile offsets, truncate and setattr. Network4 now passes
  all 64 Linux checks; native ABI passes 13 checks. No descriptor panic remains
  in the reviewed paths; unsupported operations return explicit errors.
- Reviewer P2: real-stack host test RCVBUF8/send9 failed to receive (RED log
  /tmp/kunix-phase4-smallbuf-red.log). TCP receive capacity now rounds up to
  TCP_WND so a whole raw callback pbuf can enter an empty queue. UDP limits keep
  whole-message drop semantics. Regression passes with ASan/UBSan (green2 log).
- Added external Ethernet transfer while another user process runs only RDTSC
  and PAUSE, to verify maintenance scheduling under CPU load. Final run pending.
- Final SSH run now requests a real pty and verifies test -t 0 in its authenticated
  command. Build9 packages all fixes; full final tests remain required.

### WiFi ledger (AX200, QEMU-only constraint)

- Online research: AX200 is PCI 8086:2723, Intel device family 22000
  (discrete), iwlwifi-supported since Linux 5.1, firmware `iwlwifi-cc-a0`
  API 77. No 802.11 device model exists in QEMU; the card speaks to signed
  firmware over DMA command queues, not raw 802.11 registers. Firmware
  blobs are redistribution-restricted (Intel license; OpenBSD ships the
  iwx driver but fetches firmware separately).
- `drivers/iwx200.c` + `include/iwx200.h`: honest bring-up to the
  firmware-alive state. PCI probe for 8086:2723, 128 KiB BAR validation,
  NIC-ready/prepare, software reset, clock init, persistence-bit clearing,
  strap MAC validation, RF-kill refusal, INIT TLV parsing, context-info
  load, alive poll. Every wait bounded by the TSC clock; absent card or
  missing firmware refuses cleanly, never hangs. RX/TX queues, scan/auth
  firmware commands and the RT image arrive later with hardware access.
- `kernel/wifi.c` + `include/wifi.h`: hardware-independent 802.11 station
  core. Frame parse/build, open-system auth/assoc state machine, AID
  capture, deauth handling, 802.11-to-Ethernet data conversion for lwIP.
- Host tests: `wifi_protocol` (frame layer, auth/assoc, data conversion)
  and `iwx200_protocol` (PCI/CSR/TLV/alive against stub MMIO/DMA), both in
  `test_host.py`; inet-host stubs extended. ASan/UBSan clean.
- `net_init` probes the AX200 when no e1000 is present; QEMU (no AX200)
  behavior is unchanged: 8/8 suites, network 67+13, fsck clean.
- No crypto in tree yet: WPA2 4-way handshake and CCMP are the next step
  before any association can carry encrypted traffic.
