#!/bin/sh
# This file has been generated in whole or in part by a Large Language Model
# Assemble a new RAM-root BIOS ISO using this guest's existing bootstrap tools.
set -eu
export PATH="${SELFHOST_PREFIX:-/opt/selfhost}/bin:$PATH"
work=build/selfhost-image
seed=${SELFHOST_SEED_ROOT:-/}
seed=${seed%/}
[ -x "$seed/bin/sh" ] || { echo "missing bootstrap shell: $seed/bin/sh" >&2; exit 1; }
iso=${SELFHOST_IMAGE_DIR:-$work/iso}
output=${SELFHOST_IMAGE_OUTPUT:-$work/kunix-selfhost.iso}
# This directory contains only generated root staging files. Recreate it so
# removed programs and configuration do not survive into subsequent images.
rm -rf "$work/root"
mkdir -p "$work/root/bin" "$work/root/sbin" "$work/root/dev" "$work/root/proc" "$work/root/tmp" "$work/root/home/root" "$work/root/var/log" "$work/root/var/run" "$work/root/var/tmp" "$iso/boot/limine"
# Native plain names may be hard links, copies or symlinks to name.elf.
# A different ported command with the same name remains the selected command.
seed_native_alias() {
    [ -f "$seed/bin/$1.elf" ] || return 1
    [ "$seed/bin/$1" -ef "$seed/bin/busybox" ] && return 1
    [ "$seed/bin/$1" -ef "$seed/bin/$1.elf" ] || cmp -s "$seed/bin/$1" "$seed/bin/$1.elf"
}
# Keep one BusyBox inode even when sources live on a separate /home filesystem.
# Other seed files may be linked locally or copied across filesystem boundaries.
if [ -f "$seed/bin/busybox" ]; then
    cp -pP "$seed/bin/busybox" "$work/root/bin/busybox"
fi
for program in "$seed"/bin/*; do
    name=${program##*/}
    case $name in *.elf|gcc|cc|as|ld|ar|ranlib|nm|strip|objcopy|objdump|readelf|make|selfhost-test) continue ;; esac
    if seed_native_alias "$name"; then continue; fi
    if [ -e "$work/root/bin/$name" ] || [ -L "$work/root/bin/$name" ]; then continue; fi
    if [ "$program" -ef "$seed/bin/busybox" ]; then
        ln "$work/root/bin/busybox" "$work/root/bin/$name"
    else
        ln "$program" "$work/root/bin/$name" 2>/dev/null || cp -pP "$program" "$work/root/bin/$name"
    fi
done
cp -R etc/. "$work/root/etc"
mkdir -p "$work/root/usr"
if [ -d "$seed/usr/share" ]; then cp -R "$seed/usr/share" "$work/root/usr/"; fi
if [ -d "$seed/usr/bin" ]; then cp -R "$seed/usr/bin" "$work/root/usr/"; fi
for binary in build/user/*.elf; do
    name=${binary##*/}; name=${name%.elf}
    # Always install the result of this build under its native .elf name.
    # Unlink first: the seeded entry may share an inode with the running root.
    rm -f "$work/root/bin/$name.elf"
    cp "$binary" "$work/root/bin/$name.elf"
    # BusyBox and distinct ported programs keep their command names. Native
    # aliases follow this build; deleted native sources stay out of the image.
    if { [ ! -e "$seed/bin/$name" ] && [ ! -L "$seed/bin/$name" ]; } || seed_native_alias "$name"; then
        rm -f "$work/root/bin/$name"
        ln "$work/root/bin/$name.elf" "$work/root/bin/$name"
    fi
done
for binary in build/pm/*; do
    [ -f "$binary" ] || continue
    name=${binary##*/}
    rm -f "$work/root/bin/$name"
    cp "$binary" "$work/root/bin/$name"
done
for script in compat/primemeridian/tests/*.zsh; do
    name=${script##*/}; name=${name%.zsh}
    rm -f "$work/root/bin/$name"
    cp "$script" "$work/root/bin/$name"
    chmod 755 "$work/root/bin/$name"
done
cp build/habilis/habilis "$work/root/sbin/init"
chmod 700 "$work/root/home/root"
if [ -d "$seed/test" ]; then cp -R "$seed/test" "$work/root/"; fi
if [ -d "$seed/lib/firmware" ]; then mkdir -p "$work/root/lib"; cp -R "$seed/lib/firmware" "$work/root/lib/"; fi
makefs -t ffs -o version=2 -s 128m -f 8192 "$iso/boot/root.img" "$work/root"
cp build/binaries/kunix-kodiak.elf "$iso/boot/"
cp limine/limine-bios.sys limine/limine-bios-cd.bin "$iso/boot/limine/"
printf 'timeout: 0\n\n/Kunix built inside Kunix\n    protocol: limine\n    kernel_path: boot():/boot/kunix-kodiak.elf\n    module_path: boot():/boot/root.img\n' > "$iso/boot/limine/limine.conf"
makefs -t cd9660 -o rockridge -o "bootimage=i386;$iso/boot/limine/limine-bios-cd.bin" -o no-emul-boot -o boot-info-table "$output" "$iso"
limine bios-install "$output"
size=$(wc -c < "$output")
echo "SELFHOST IMAGE PASS: $output ($size bytes)"
echo "ISO_BYTES $size"
