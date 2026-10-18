#!/bin/sh
# This file has been generated in whole or in part by a Large Language Model
# Optional development ISO. Build the normal userland/root staging tree first.
set -eu
SELFHOST_DIR=${SELFHOST_DIR:-"$(pwd)/build/selfhost"}
[ -x "$SELFHOST_DIR/toolchain/bin/make" ] || { echo 'Run buildutils/selfhost-toolchain.sh first' >&2; exit 1; }
[ -x build/ufsroot/bin/sh ] || { echo 'Run buildutils/build.sh build first' >&2; exit 1; }
mkdir -p "$SELFHOST_DIR"
staging=$(mktemp -d "$SELFHOST_DIR/rootfs.XXXXXX")
trap 'rm -rf "$staging"' EXIT HUP INT TERM
cp -a build/ufsroot/. "$staging/"
python3 buildutils/selfhost.py stage "$staging" --toolchain "$SELFHOST_DIR/toolchain"
makefs -t ffs -o version=2 -s 768m -f 49152 "$SELFHOST_DIR/root.img" "$staging"
python3 buildutils/ufschown.py "$SELFHOST_DIR/root.img" 0 0
mkdir -p "$SELFHOST_DIR/iso_root/boot/limine"
cp build/binaries/kunix-kodiak.elf "$SELFHOST_DIR/iso_root/boot/"
cp "$SELFHOST_DIR/root.img" "$SELFHOST_DIR/iso_root/boot/root.img"
cp limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin "$SELFHOST_DIR/iso_root/boot/limine/"
printf 'timeout: 0\n\n/Kunix selfhosting development kit\n    protocol: limine\n    kernel_path: boot():/boot/kunix-kodiak.elf\n    module_path: boot():/boot/root.img\n' > "$SELFHOST_DIR/iso_root/boot/limine/limine.conf"
xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table --efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image -o "$SELFHOST_DIR/live.iso" "$SELFHOST_DIR/iso_root"
./limine/limine bios-install "$SELFHOST_DIR/live.iso"
echo "Self-host development ISO: $SELFHOST_DIR/live.iso"
