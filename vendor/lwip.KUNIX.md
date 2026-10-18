<!-- This file has been generated in whole or in part by a Large Language Model -->
# lwIP provenance and Kunix integration

- Upstream: https://savannah.nongnu.org/projects/lwip/
- Release: **2.2.1**, tag `STABLE-2_2_1_RELEASE`.
- Official mirror archive: https://codeload.github.com/lwip-tcpip/lwip/tar.gz/refs/tags/STABLE-2_2_1_RELEASE
- Archive SHA-256: `ce0b7461c0ad9602c376f0bf07c5eb7253b48c7bf66f011c6bf3e2a96731c539`.
- License: upstream BSD terms, retained in `lwip/COPYING` and source headers;
  build.sh also copies COPYING into the guest's `/usr/share/licenses/lwip/`.

The upstream tree is unmodified. Kunix's freestanding configuration and libc
headers live in `include/lwip-port/`; the raw callback integration lives in
`kernel/inet.c`. The e1000 driver and native/Linux ABI adapters do not expose
lwIP headers. NO_SYS calls run with IRQ exclusion on one CPU, never from an IRQ;
callbacks do not block. This assumption must be revisited before SMP.

`sh buildutils/tests/inet-host/run.sh` runs the real stack under sanitizers.
`python3 buildutils/network_test.py` exercises the integrated stack in QEMU.
