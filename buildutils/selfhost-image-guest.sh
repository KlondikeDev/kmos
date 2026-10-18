#!/bin/sh
# This file has been generated in whole or in part by a Large Language Model
# Assemble a new RAM-root BIOS ISO using this guest's existing bootstrap tools.
set -eu
export PATH="${SELFHOST_PREFIX:-/opt/selfhost}/bin:$PATH"
work=build/selfhost-image
iso=${SELFHOST_IMAGE_DIR:-$work/iso}
output=${SELFHOST_IMAGE_OUTPUT:-$work/kunix-selfhost.iso}
mkdir -p "$work/root/bin" "$work/root/sbin" "$work/root/dev" "$work/root/proc" "$work/root/tmp" "$work/root/home/root" "$work/root/var/log" "$work/root/var/run" "$work/root/var/tmp" "$iso/boot/limine"
# Hard links preserve BusyBox's hundreds of applets without copying each binary.
# Replace new programs by unlinking first, so the running root stays untouched.
for program in /bin/*; do
    name=${program##*/}
    case $name in gcc|cc|as|ld|ar|ranlib|nm|strip|objcopy|objdump|readelf|make|selfhost-test) continue ;; esac
    [ -e "$work/root/bin/$name" ] || ln "$program" "$work/root/bin/$name"
done
cp -R etc/. "$work/root/etc"
mkdir -p "$work/root/usr"
if [ -d /usr/share ]; then cp -R /usr/share "$work/root/usr/"; fi
if [ -d /usr/bin ]; then cp -R /usr/bin "$work/root/usr/"; fi
for binary in build/user/*.elf; do
    name=${binary##*/}; name=${name%.elf}
    # Stock policy, matching the host build: BusyBox applets keep their
    # names (the suites assume their semantics); the native build is kept
    # under its plain name only where nothing else provides it. The .elf
    # names are preserved by the hardlink pass above.
    if [ ! -e "$work/root/bin/$name" ]; then
        cp "$binary" "$work/root/bin/$name"
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
if [ -d /test ]; then cp -R /test "$work/root/"; fi
if [ -d /lib/firmware ]; then mkdir -p "$work/root/lib"; cp -R /lib/firmware "$work/root/lib/"; fi
makefs -t ffs -o version=2 -s 128m -f 8192 "$iso/boot/root.img" "$work/root"
cp build/binaries/kunix-kodiak.elf "$iso/boot/"
cp limine/limine-bios.sys limine/limine-bios-cd.bin "$iso/boot/limine/"
printf 'timeout: 0\n\n/Kunix built inside Kunix\n    protocol: limine\n    kernel_path: boot():/boot/kunix-kodiak.elf\n    module_path: boot():/boot/root.img\n' > "$iso/boot/limine/limine.conf"
makefs -t cd9660 -o rockridge -o "bootimage=i386;$iso/boot/limine/limine-bios-cd.bin" -o no-emul-boot -o boot-info-table "$output" "$iso"
limine bios-install "$output"
size=$(wc -c < "$output")
echo "SELFHOST IMAGE PASS: $output ($size bytes)"
echo "ISO_BYTES $size"
