<!-- This file has been generated in whole or in part by a Large Language Model -->

# Disk cache, demand paging and battery implementation plan

**Goal:** Continue Phase 2 while adding the requested T14 battery support.

**Spec:** ROADMAP.md Phase 2 and the owner's battery request. The owner explicitly
asked to keep going; execute without another planning approval gate.

**Architecture:** Keep the native Kunix ABI and share kernel services with the
Linux personality. Add a bounded write-back block cache below the filesystems,
a bounded clean-page cache for VM-backed files, and file-backed VMAs for mmap
and static ELF segments. Hold open vnode references until the last mapping
goes away. Evaluate firmware battery methods through uACPI with a bounded EC
adapter, exposing a shared proc file and native battery command.

## Ownership

- Disk agent: disk/ATA layer, FAT routing, block-cache tests.
- Battery agent: interpreter adapter, battery normalization/query, native command.
- Parent: VMAs, page cache, ELF, file/vnode lifetime, VFS invalidation and sync,
  process cleanup/fork/exec, build/boot/proc integration, integration tests.

## Review focus

- Failed exec must retain the entire old image, including VMAs and file references.
- Closed descriptors, unlink, fork, split/merge and failed mappings must not
  free file backing early or leak references.
- Cached clean file frames must never become writable without private copying.
- Partial ELF pages and BSS must contain zeros, including overlapping PT_LOADs.
- EOF/truncation and write invalidation must not expose recycled inode data.
- Flush failures must keep dirty data and propagate failure; shutdown must
  persist data before marking a filesystem clean.
- Battery methods must not run in IRQ context or reenter when PID 0 is preempted;
  absent/unknown batteries must remain explicit rather than fabricated readings.

## Tasks

- [x] Record pre-change boot and X timings, and add integration tests that expose
  eager file mappings/ELF loading and mapping lifetime/coherence faults.
- [x] Add clean page cache, VMA file backing/lifetime, lazy mmap and private COW;
  test fork, close/unlink, mprotect, split/merge, EOF and invalidation.
- [x] Load ELF segments as VMAs, preserve transactional exec rollback, raise
  image/page limits, and test sparse large executable BSS and failed exec.
- [x] Integrate block cache, explicit native/Linux sync/fsync, periodic deferred
  flush and safe shutdown ordering; verify persistence and fault-injected flush.
- [x] Integrate battery firmware adapter, shared proc view and native command;
  test normalization, no-battery and firmware query paths.
- [x] Build, run host and all QEMU suites, repeat boot/X measurements, request
  independent review, update roadmap evidence, and commit passing changes.

## Verification and review

- Before the changes, the VM integration test failed on eager large mappings,
  missing EOF SIGBUS and the large ELF BSS limit. The expanded VM suite now
  passes all 17 checks.
- Independent review found two fault-filling bugs: an uncached sole-owner frame
  was incorrectly forced through COW, and later overlapping ELF BSS did not
  overwrite earlier file bytes. The production-code host fixture reproduced
  both failures, then passed after the fixes; review confirmed the fixes.
- Seven host sanitizer checks pass, covering production block-cache failure
  retries, LRU and periodic flushing; page-cache lifetime/invalidation; fault
  filling; battery normalization; ACPI parsing; logs; and cache attributes.
- Real uACPI execution with injected SSDTs passes five normal battery checks
  and six low-battery checks. UEFI passes the normal battery fixture. Ordinary
  QEMU reports zero batteries. BIOS and UEFI Q35 reset checks and UEFI PC S5
  shutdown pass. Q35 needs the live image because its SATA controller is not
  supported yet; PC firmware lacks an ACPI reset register and uses fallback.
- The first full suite run passed seven suites, but the UFS harness killed the
  VM before its write-back cache flushed. The test now explicitly checks sync
  before its summary, defining the intended persistence boundary.
- T14 battery/EC, laptop power controls and hardware X performance remain
  pending access to the actual laptop.

## Performance experiments

The first comparison improved boot but regressed tick-based X startup timing.
Four-probe hashed page-cache sets reduced lookup work and retained correctness.
Executable read-ahead and ATA string I/O experiments did not improve that
measurement and were removed. ELF faults still load one page at a time.
Because guest PIT ticks can miss IRQ-off disk time, the harness now measures
X startup between explicit serial markers using host wall time at 10 ms
polling intervals. The baseline kernel was rebuilt from Phase 1 commit e669d7d
for comparison with identical current userland and QEMU settings.

The controlled host comparison measured console startup at 7.612 → 6.210
seconds and X plus xterm readiness at 2.300 → 1.286 seconds, with 256 MiB
RAM and identical starting disk images/userland. Baseline guest uptime gave
0.880 seconds for the same 2.300-second interval, demonstrating why it was
unsuitable. These are individual QEMU runs, not T14 measurements.

## Final result

The final build has no compiler warnings/errors (only the existing linker RWX
warning). All eight QEMU suites pass, totaling 378 checks; UFS write/reboot
verification and fsck are clean. Host sanitizer programs and the hashed-cache
variant pass. Independent review findings were resolved and re-reviewed.
The live ISO is rebuilt at `build/binaries/kunix-live.iso`. T14 battery/EC and
hardware power/X checks remain pending; NVMe/GPT and the installer are next.
