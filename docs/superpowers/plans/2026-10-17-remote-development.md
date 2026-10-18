<!-- This file has been generated in whole or in part by a Large Language Model -->
# Remote development implementation plan

> **For agentic workers:** implement each independent subsystem with tests, then integrate and review.

**Goal:** Operate the T14 over inbound SSH, reconnect saved Wi-Fi, use ping, build Kunix on Kunix, and apply remote updates while retaining installed data and a boot fallback.

**Architecture:** Extend the existing lwIP socket service and Habilis. Keep credentials, host keys, accounts and development files on the separate home filesystem. Updates stage a kernel and pristine root image in an inactive EFI generation; retain the original installed root and previous generation, verify staged bytes, then change boot selection. The updated root is a RAM image with persistent home, and the original installed root remains accessible at `/installed`.

**Tech stack:** C, static musl, native Kunix ABI, Prime Meridian, lwIP, Dropbear, UFS2, FAT32, Limine, Python build tooling.

## Constraints and review focus

- Preserve native ABI; Linux sockets adapt shared kernel services.
- Do not write to mounted UFS partitions or a mounted disk's GPT.
- Never embed Wi-Fi passwords, private SSH keys or account passwords in images.
- Verify interrupted staging leaves the current generation intact; retain an independent original boot entry.
- A single FAT boot-selection write still depends on storage honoring flushes; USB recovery remains available after damaged EFI metadata. Do not claim power-loss atomicity that the filesystem cannot provide.
- Preserve all original installed root data; home is shared by every generation. Generation boot does not silently replace home with an empty image.

## Tasks

- [x] ICMP raw sockets: privilege, loopback, endpoint isolation, bounded receive queues, wire fixtures, BusyBox ping in QEMU. Verified: host suites plus guest `nettest` 69/69 and `network_test.py` (external TCP/UDP/DNS/HTTP, native personality).
- [x] Persistent network: protected atomically saved profiles, boot reconnect with backoff, explicit disconnect/forget; inbound Dropbear service with persistent keys and safe initial authentication. Verified: `ssh_service_test.py` two-boot PTY login, persistent identity.
- [x] Self-hosting: pinned static hosted compiler/binutils and make, source kit on home, guest kernel/native userland builds, boot the resulting kernel. Verified end to end: `selfhost_test.py` builds kernel+userland+ISO in QEMU, exports with SHA256 match, and passes ktest/pmtest/pmsmoke/permtest/fbtest/xtest/vmtest on the guest-built ISO. Two assembly bugs fixed along the way: guest /tmp too small for staging (workspace moved to /var/tmp, dev root grown to 768m) and unflushed NVMe export (dd conv=fsync, exact byte count reporting).
- [x] Update bundle and EFI writer: exact format/checksums; validate source image and Bernard ESP; allocate inactive payloads without touching existing file chains; verify readback before boot selection; retain previous and original entries; rollback command. Verified: `update_test.py` at 512/4096 bytes plus `update_live_test.py` (`--from-live` without network).
- [x] Integration: build images, host sanitizers, network/SSH tests, disposable NVMe update/reboot/data-preservation tests all pass; stock `--all` regression run in progress at handoff.
- [ ] Document actual completion and hardware checks; update the roadmap with the owner's confirmed AX200 internet result.
