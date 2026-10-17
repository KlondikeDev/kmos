<!-- This file has been generated in whole or in part by a Large Language Model -->
# Intel AX200 firmware provenance

- File: `iwlwifi-cc-a0-71.ucode` (1,322,288 bytes, API version 71)
- SHA-256: `1561efa63710678be1392b0a572b9b5d021b529e71463346a7a05129294ddfa6`
- Source: Ubuntu `linux-firmware_1.187.39_all.deb`
  (`http://archive.ubuntu.com/ubuntu/pool/main/l/linux-firmware/`),
  extracted from `lib/firmware/iwlwifi-cc-a0-71.ucode`.
- Upstream: Intel wireless firmware, `linux-firmware` tree.
- License: `LICENCE.iwlwifi_firmware` (Intel binary redistribution terms:
  binary form permitted with copyright notice; no reverse engineering).
  Retained here and copied into the guest's `/usr/share/licenses/iwlwifi/`
  by build.sh alongside the blob.
- The driver parses this image with `iwx200_parse_fw` and loads its
  REGULAR sections at AX200 probe. API 71 predates the driver's reference
  API 77; command versions are negotiated from the firmware's own TLVs.
