<!-- This file has been generated in whole or in part by a Large Language Model -->
# Testing AX200 on the T14

Boot the rebuilt `build/binaries/kunix-live.iso` and log in as root. The live image includes the firmware and tools; installation is not required to test the driver.

```
wifi status
wifi scan
wifi join "your SSID"
netinfo
```

`join` prompts for the passphrase with terminal echo disabled. For an open AP, use `wifi join "your SSID" --open`. A successful join saves the profile securely on the separate home disk (see below). After association, allow DHCP time to assign an address; repeat `netinfo` until address, gateway and DNS appear. `nslookup example.com` and `wget -O /tmp/wifi-check http://example.com/` exercise DNS and TCP/HTTP. `wifi disconnect` drops the link and its DHCP configuration.

For failures, capture these small reports:

```
wifi status
cat /proc/netinfo
cat /proc/version
```

Capture the complete retained boot log without the screen or dmesg output-size limit:

```
cat /proc/kmsg > /tmp/boot.log
more /tmp/boot.log
grep -iE 'iwx|wifi|wlan' /tmp/boot.log
```

The kernel retains the latest 65,536 bytes. `dmesg -s 65536` also requests that capacity. `firmware Notify(...VGA_...)` messages are ACPI graphics notifications, unrelated to AX200. The short status report exposes card detection, firmware bootstrap stage, errno, API and diagnostic CSR values even if a log line has scrolled away.

`wifitest` checks packaging, status/control ABI and interface consistency. Passing it alone does not establish RF scanning, association, DHCP or internet access. These require the physical card and an AP; the host event fixtures and QEMU cannot establish hardware success.

## First candidate limits

- Discrete Intel AX200 `8086:2723`, bundled `iwlwifi-cc-a0-71.ucode` only.
- Passive scanning on permitted 2.4/5 GHz channels; association requires an active, non-DFS channel.
- Legacy 20 MHz rates, without aggregation or HT/VHT/HE throughput.
- Open networks and WPA2-PSK with CCMP. No WPA3, required PMF, enterprise authentication, TKIP or WEP.
- No hidden-SSID active probing, roaming or suspend/resume recovery.
- Group-key rotation is supported. A new pairwise-key handshake disconnects and wipes keys; the Wi-Fi manager reconnects automatically unless paused. Repeated messages from the existing handshake do not reinstall keys.

Hardware firmware acceptance, RF operation and sustained traffic remain pending until the T14 results are available.

## Saved Wi-Fi and inbound SSH

The Wi-Fi manager requires a separate, writable `/home` disk. A live image
without that disk can scan and report status; joining or saving credentials
fails closed. A successful `wifi join "your SSID"` now saves one profile in
`/home/.kunix/wifi`, owned by root with mode 0600, inside a root-only directory. The manager also removes group and other write
permission from the root-owned `/home` mount directory before trusting saved
settings, including older home images created with a permissive host umask.
The passphrase travels through the terminal and device ioctl, never a command
argument or log. The profile is plaintext protected by filesystem permissions;
protect the home disk accordingly. Files with unsafe ownership, permissions,
links or malformed contents are refused.

Habilis starts the manager automatically. On reboot it reloads the saved
profile; after link loss it retries with delays of 1, 2, 4, 8, 16, 32, then 60
seconds. `wifi disconnect` persistently pauses reconnection, including across
reboot. `wifi reconnect` joins the saved network and resumes automatic recovery.
`wifi forget` disconnects, pauses recovery and removes the saved profile.
DHCP remains the kernel's responsibility after association.

Inbound SSH uses the packaged Dropbear server. It starts disabled because the
shipped root account has an empty console password. On the Kunix console:

```
passwd
sshctl enable
sshctl status
```

Alternatively, put a public key in `/home/root/.ssh/authorized_keys`, with root
ownership and no group or other write access on that file and its parent
directories, then run `sshctl enable`. The key must begin with `ssh-ed25519`,
`ssh-rsa`, or `ecdsa-sha2-`; key lines beginning with options are accepted by
Dropbear but do not by themselves satisfy this conservative enable check.
Never enable empty-password authentication. From another computer use
`ssh root@KUNIX_IP` after `netinfo` shows an address. The SSH service binds TCP
port 22 on available interfaces, including Ethernet, and keeps its Ed25519
host key in `/home/.kunix/ssh_host_ed25519_key`. Rebuilding the root image
preserves that identity and the enable setting on the home disk.

`sshctl disable` stops the listener within two seconds and persists across
reboot. Established sessions may continue until they log out. Habilis supervises
the manager, which starts Dropbear only with valid saved enable state and local
authentication configured. If the home disk or authentication becomes
unavailable, the manager stops the listener. The host key is created at enable
time; boot reuses it. Missing Dropbear binaries make enable fail explicitly.

Validate persistence on the T14 by joining, enabling SSH, recording the SSH host
fingerprint, rebooting twice, and confirming automatic Wi-Fi association, DHCP,
SSH access and an unchanged fingerprint. Turn the AP off and back on to check
recovery; then disconnect manually and verify it remains disconnected until
`wifi reconnect`. Also boot without the home disk and verify no SSH listener
starts and no credentials are saved to the root image. Host tests cover saved
profiles, permissions, symlink rejection, profile reload in a fresh process,
SSH enable-state permission checks, host-key reuse and retry delays; they do
not establish firmware, RF operation or end-to-end persistent boot behavior.

The Habilis SSH integration check is `python3 buildutils/ssh_service_test.py`
after rebuilding the live ISO. It creates an empty disposable 64 MiB home disk
and a temporary SSH identity, enables the actual booted service, exercises an
OpenSSH PTY login and service disable/enable, then boots the same fixture again.
The second boot must accept the saved key without another enable command and
pass strict host-key verification against the first boot's identity. It never
attaches the repository's real `home.img` or reads the host's SSH keys. Logs are
under `build/phase4/ssh-service-test`; fixture disks and private credentials are
deleted on completion. `--self-test` checks the host harness's identity policy
and protections against attaching the real home image.
