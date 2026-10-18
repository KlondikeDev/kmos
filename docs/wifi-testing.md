<!-- This file has been generated in whole or in part by a Large Language Model -->
# Testing AX200 on the T14

Boot the rebuilt `build/binaries/kunix-live.iso` and log in as root. The live image includes the firmware and tools; installation is not required to test the driver.

```
wifi status
wifi scan
wifi join "your SSID"
netinfo
```

`join` prompts for the passphrase with terminal echo disabled. For an open AP, use `wifi join "your SSID" --open`. It never saves the passphrase. After association, allow DHCP time to assign an address; repeat `netinfo` until address, gateway and DNS appear. `nslookup example.com` and `wget -O /tmp/wifi-check http://example.com/` exercise DNS and TCP/HTTP. `wifi disconnect` drops the link and its DHCP configuration.

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
- Group-key rotation is supported. A new pairwise-key handshake disconnects and wipes keys; use `wifi join` again. Repeated messages from the existing handshake do not reinstall keys.

Hardware firmware acceptance, RF operation and sustained traffic remain pending until the T14 results are available.
