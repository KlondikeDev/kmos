<!-- This file has been generated in whole or in part by a Large Language Model -->
# Remote operation and base updates

## First upgrade of an existing installation

Boot the new live USB and run:

```
kunix-update --esp /dev/disk16 --from-live
sync
reboot
```

Use the actual Bernard EFI slice if it is not `/dev/disk16`. This enrolls the
new live system as a boot generation using the pristine bootloader root image;
it does not run the installer, format UFS, or copy your temporary live session
into the update. Remove the USB to boot the selected generation. Your existing
home is selected by its installed GUID, and the original root is retained.
An initial USB boot is needed because the old installed kernel predates the
EFI-update interface and services. Subsequent updates can use SSH throughout.

## Establish remote access once

Boot an image containing `netconfig` and the Habilis Wi-Fi/SSH services. On the
installed system with its separate home partition mounted:

```
wifi join "YOUR SSID"
passwd
sshctl enable
wifi status
cat /proc/netinfo
```

`wifi join` prompts without echo and saves a successful profile. Habilis joins
it at boot and retries after link loss with bounded backoff. `wifi disconnect`
pauses reconnection persistently; `wifi reconnect` resumes it. `wifi forget`
removes the profile. The current manager saves one network.

From another machine use `ssh root@ADDRESS` (port22). SSH starts only after
`sshctl enable` and a nonempty root password or installed authorized public
key. For key authentication, put your public key in
`/home/root/.ssh/authorized_keys`, with root-owned, non-writable-by-others parent
directories. The daemon disallows the shipped empty password. `sshctl disable`
stops the listener. Wi-Fi credentials and SSH host identity are root-only files
under `/home/.kunix`; the home partition and account files survive base updates.

No upstream SSH server or outbound SSH connection is required. Modern `scp`
uses SFTP, which is not packaged: transfer through an authenticated SSH pipe.

## Build and transfer an update

Create a bundle from a completed, tested base build on the development machine.
Rebuild the root image first: any QEMU boot of `ufs.img` clears the
cleanly-unmounted flag, and the updater refuses bundles whose root image
lacks it. Run the `ufs` and `setup` steps of `buildutils/build.sh`, then:

```
python3 buildutils/update-bundle.py build/binaries/kunix-kodiak.elf ufs.img build/kunix-update.pkg
cat build/kunix-update.pkg | ssh root@ADDRESS 'cat > /home/kunix-update.pkg.part && mv /home/kunix-update.pkg.part /home/kunix-update.pkg'
```

A bundle contains a fixed512-byte header, exact kernel/root lengths, and SHA256
hashes of both payloads. Transfer it over SSH or another authenticated channel;
the embedded hashes detect corruption and do not authenticate an untrusted
publisher. There is no public package repository or automatic downloading yet.

On Kunix, identify the Bernard installer's EFI slice (normally `/dev/disk16`
for the first GPT disk). It must be the512MiB FAT32 partition, GPT slot1,
beside the matching UFS root and home partitions:

```
kunix-update --esp /dev/disk16 /home/kunix-update.pkg
sync
reboot
```

The updater verifies the entire bundle before touching the EFI partition,
stages an inactive kernel and root image, flushes and verifies readback, then
selects it. It never repartitions or writes a mounted UFS partition. Both the
root and home GUIDs must match that disk's installed partitions. Concurrent
updaters are refused. A generation must actually boot before it can stage
another update. A failed interrupted updater can leave
`/tmp/kunix-update.lock`; remove that directory only after confirming no updater
is still running.

First enrollment initializes two EFI slots. Subsequent updates reuse only the
inactive slot. A flushed per-slot READY record is cleared before staging and
written only after payload verification. Invalid slots are removed from the
boot menu during staging, and rollback rechecks the complete payload hashes.

## Data layout and rollback

The new generation uses a pristine128MiB root loaded into RAM. **Keep new work
and configuration that must persist under `/home`.** Writes elsewhere in the
new generation's RAM root do not survive reboot. Account state, Wi-Fi profiles
and SSH host keys already use home. The original installed root is retained in
full and mounted at `/installed`; existing files and custom configuration there
are accessible and are not overwritten by a base update. Custom original
`/etc` files are retained at `/installed/etc`, not automatically merged into the
new image's `/etc`.

This arrangement permits updates of today's single-root installations without
shrinking or formatting them. It is a bootstrap generation mechanism; a future
persistent generation/overlay system can improve root configuration handling.
It is not a general arf package manager or a complete persistent root upgrade.

If a new generation boots but needs to be undone:

```
kunix-update --esp /dev/disk16 --rollback
reboot
```

The Limine menu also retains the previous valid generation and the original
installed kernel/root. Rollback does not roll back shared home data.

**Recovery limit:** FAT32 is not journaled. Payload staging leaves the selected
payload intact, but a torn FAT/enrollment or boot-selection write can require
the recovery USB to repair the EFI partition. The separate installed UFS root
and home are not overwritten. Keep the USB until the new generation has booted
and remote access is confirmed. This is not a guarantee against storage failure
or power loss during every possible metadata write.

Current fixed limits are a4MiB kernel and128MiB base root, with two generations
on the installer's512MiB ESP. Unsupported FAT layouts, crosslinked chains,
conflicting aliases and mounted EFI filesystems are refused.

## Validation

Host checks cover checksums, exact sizes, FAT geometry at512/4096 bytes,
independent cluster ownership, malformed names/chains, interrupted staging,
retained current/original payloads and corrupted rollback refusal. The disposable
NVMe integration harness is `python3 buildutils/update_test.py`; it installs,
updates, boots both generations, rolls back, and checks original root and home
sentinels. `python3 buildutils/ssh_service_test.py` checks actual inbound Habilis
SSH over two boots with the same persistent host key.
