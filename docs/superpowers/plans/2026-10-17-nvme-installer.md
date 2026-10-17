<!-- This file has been generated in whole or in part by a Large Language Model -->
# NVMe, GPT and installer implementation plan

**Goal:** Install Bernard from the live image onto an NVMe GPT disk, boot its
UFS2 root by PARTUUID, and retain a separate home across reboots.

**Architecture:** Keep disk_read/write's 512-byte addressing. Add bounded,
polled NVMe queues with page-sized DMA buffers and native-sector translation;
register GPT slices in the disk layer with coherent physical-disk caching.
Use protected raw devfs nodes for a root-only text installer. Copy an immutable
live-root image, grow UFS2 offline, format the ESP, and install Limine UEFI.

**Spec:** ROADMAP.md Phase 3 and the approved sequence in chat. The owner has
already asked to proceed without more planning questions. Work in place on
prime-meridian; initial dirty generated disk.img/ELF are backed up in
build/phase3-start and must not enter the source commit.

## Interfaces and ownership

- NVMe worker: drivers/nvme.c, include/nvme.h and driver-focused tests only.
  nvme_init(); nvme_count(); nvme_sectors(index) in 512-byte units;
  nvme_block_size(index) native 512/4096; nvme_read/write(index,lba,count,buf);
  nvme_flush(index). All return bool except count u32, sectors u64, size u32.
- GPT worker: kernel/gpt.c, include/gpt.h and parser host tests only.
  gpt_scan(u8 disk,gpt_partition_t* out,u32 cap) returns count or negative errno;
  entries carry original slot, parent, start/count in 512-byte units and GUIDs.
  Consume disk_block_size(disk) for native GPT LBAs. GUID parse/format helpers.
- UFS grow worker: compat/primemeridian/tests/ufsgrow.c and host integrity tests only. Standalone
  musl-compatible CLI: ufsgrow DEVICE SIZE_BYTES; copy existing UFS2 geometry,
  add groups/extend final group, relocate summaries as needed, update backups,
  and preserve every existing inode. Refuse shrinking/invalid geometry.
- Parent: disk registry/slices, raw device permissions and size ioctls, boot
  selectors, immutable live source, installer/FAT writer, build and VM tests.

## Safety and review focus

- Reject invalid/overlapping GPT metadata, bad CRCs, overflow and out-of-range
  slices; verify the backup before falling back from a damaged primary.
- Timeout/fatal NVMe completion must never reuse/free DMA still owned by the
  controller. Handle queue wraps and 512/4096-byte namespace formats.
- Slice and whole-disk access must share the same cached physical blocks.
- Installer refuses mounted targets and unprivileged callers; no writes before
  the complete partition plan and exact typed YES. Test only disposable images.
- Installation source stays pristine even while the live root is modified.
- UFS growth preserves allocation maps, summaries and backups; fsck validates
  grown images, including partially populated final groups.

## Tasks

- [x] Implement/test polled NVMe discovery, identify, read/write and flush.
- [x] Implement/test CRC-checked GPT parsing, backup recovery and GUID lookup.
- [x] Integrate disk slots: ATA 0–3, RAM 4–7, NVMe 8–15, GPT 16–127;
  partitions translate before caching. Add protected devfs raw nodes and ioctls.
- [x] Implement root=/home=PARTUUID selectors, refusing silent fallback when
  an explicit selector is invalid; preserve existing default ATA/live boot.
- [x] Implement/test offline UFS2 growth and an empty home template.
- [x] Preserve boot module originals using RAM-disk copy-on-write; expose a
  read-only install source and package kernel/BOOTX64.EFI/home template.
- [x] Implement blue/white installer, plan confirmation, GPT and FAT32 ESP
  creation, root/home copying/growth, flush and Limine configuration.
- [x] Test install cancellation/permissions, NVMe 512/4096 I/O and GPT faults;
  install into a disposable VM disk, boot UEFI without ISO, verify persistent
  root/home, and run all existing suites plus host sanitizers/fsck.
- [x] Review, fix findings, update roadmap and commit passing source changes.

## Execution notes

Use the parallel-agent skill for three independent components, then integrate
and verify in the parent. Hardware installation remains the owner's action;
never write the development host's real devices.


### Review and verification notes

- Independent review found that interrupted-growth markers must block mounts
  and that root/home selectors must not mount overlapping devices. Both checks
  were added, with negative UEFI boot cases in the installer matrix.
- Cache eviction batches at most 32 dirty blocks under one device FLUSH. A
  failed write or FLUSH keeps every resident copy dirty and retryable; host
  sanitizer tests cover both failure and retry. Independent review passed.
- The first regression run reproduced a NULL vnode fault in the new ioctl
  dispatch when musl opened a pty master. `devfs_is_disk` now accepts NULL;
  the existing pmtest openpty sequence is the regression test. Narrow follow-up
  review checked both ABI paths and every caller; no further findings.
- Full build log: `/tmp/kunix-phase3-build5.log`; only the pre-existing linker
  RWX segment warning. Final matrix and regression results are recorded below
  after their runs complete.


### Final results (2026-10-17)

- `python3 buildutils/test.py --all`: 8/8 suites, 378 checks, no failures;
  UFS reboot verification passed, ufsdiag reported zero problems, fsck clean.
  Log: `/tmp/kunix-phase3-all-final.log`.
- `python3 buildutils/test_host.py`: all ASan/UBSan checks passed, including
  both disk eviction modes and both page-cache modes.
  Log: `/tmp/kunix-phase3-host-final.log`.
- `python3 buildutils/tests/ufs_grow.py`: all six integrity/refusal tests passed.
  Log: `/tmp/kunix-phase3-grow-final.log`.
- Host FAT/GPT writer fixture: native 512/4096 layouts parsed by production GPT
  code; fsck.fat and mtools verified files/configuration, with sanitizers enabled.
  Log: `/tmp/kunix-phase3-fat-final.log`.
- `python3 buildutils/install_test.py --block-size 512 --reset` and the 4096
  variant: both passed every stage, including unchanged whole-image hashes for
  plan/cancel, installed UEFI boot, root/home persistence, mounted-disk write
  refusal, ACPI poweroff, three negative boot cases and all partition fsck checks.
  Detailed serial and checker logs: `build/phase3/integration-{512,4096}/`.
- Harness rejects non-regular NVMe paths before starting QEMU; `/dev/null`
  rejection checked. The development host's physical devices were never exposed.
- Source review and `git diff --check` passed. T14 NVMe installation, physical
  power controls and battery readings remain hardware checks for the owner.
