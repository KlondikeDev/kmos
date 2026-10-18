<!-- This file has been generated in whole or in part by a Large Language Model -->
# lwIP networking for Kunix

The owner selected a lwIP port to reach usable networking and SSH, retaining
Kunix's own ABI and the option to replace the protocol implementation later.
Earlier authorization says to keep working without planning approval questions.
Work remains on prime-meridian; preserve the two initially dirty generated images.

## Design

Vendor upstream lwIP 2.2.1 (BSD licensed) unchanged with provenance and license.
Use NO_SYS=1 and its raw callbacks, serialized by IRQ exclusion on Kunix's
single CPU. Never block or schedule inside a lwIP callback. Kernel socket
objects, queues, waiting, errors and descriptor lifetimes belong to Kunix.
Linux and native syscall layers translate into these shared services; a future
BSD personality can do the same. Vendor headers never enter public syscall ABI.

A bounded, polled Intel 82540EM/e1000 driver copies Ethernet frames into/out of
DMA buffers and implements no IP logic. RX processing and lwIP timeouts run in
net_poll from idle and syscall maintenance, with PID 0 maintenance scheduled at
least every 20 ms even if user code is CPU-bound. Loopback works without a NIC.
DHCP configures eth0; its DNS server updates /etc/resolv.conf only on change.
No guest network listener starts automatically. Test QEMU uses user networking,
with explicit localhost forwards for tests. No physical host device is exposed.

IPv4 TCP and UDP sockets provide bind/connect/listen/accept, file read/write,
send/recv with addresses, half-close, names, nonblocking connect, poll readiness,
SO_ERROR, reuse, NODELAY, keepalive, buffer limits and receive/send timeouts.
Unsupported IPv6 and options return explicit errors. Bound all queues and
lengths, preserve unread bytes and UDP boundaries, and retain objects through
callbacks/close/error. Privileged ports require root. SIGPIPE follows existing
socket behavior. ICMP echo is supplied by lwIP; user ping needs a separate
supported socket interface, not a pretend success.

Linux ABI supports sendmsg/recvmsg and interface query ioctls as well as ordinary
sockets; Unix-domain sockets remain intact. Native calls get independent syscall
numbers and an 8-byte address (IPv4 address and port in network byte order).
Expose network configuration/counters to native netinfo and /proc/netinfo.

## Verification and completion

Tests precede implementation: host driver protocol tests, kernel service tests
where practical, guest nettest for loopback TCP/UDP, readiness, errors, signals,
shutdown, invalid addresses, queue bounds and native ABI; QEMU DHCP/external
host-local TCP/UDP/DNS, inbound forwards, HTTP fetch and SSH interoperability.
Port static Dropbear client/server and verify a command over SSH in both
directions using disposable test credentials. Never claim production security
from weak_random; audit entropy before exposing SSH. All existing eight suites
and host sanitizers must pass; review the complete diff before committing.

T14 Ethernet implementation depends on confirming the actual PCI ID from that
laptop. QEMU support and architecture may land independently, but hardware
networking remains pending until identified and tested. Wi-Fi, IPv6, threads,
SMP and shared libraries are outside this phase.
