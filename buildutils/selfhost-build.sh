#!/bin/sh
# This file has been generated in whole or in part by a Large Language Model
# Run from /usr/src/kunix. No Python, git or image tools are needed here.
set -eu
export PATH="${SELFHOST_PREFIX:-/opt/selfhost}/kernel-bin:${SELFHOST_PREFIX:-/opt/selfhost}/bin:$PATH"
info() { printf '%s\n' "$*"; }
. ./buildutils/selfhost-commands.sh
case ${1:-kernel} in
kernel)
    build
    link
    mkdir -p build/binaries
    cp kunix-kodiak.elf build/binaries/kunix-kodiak.elf
    echo 'SELFHOST KERNEL PASS'
    ;;
userland)
    mkdir -p build/user build/pm build/habilis
    flags='-std=gnu99 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pic -Iuser'
    x86_64-elf-as user/crt0.s -o build/user/crt0.o
    x86_64-elf-gcc -c user/lib/libk.c -o build/user/libk.o $flags -fno-builtin -fno-tree-loop-distribute-patterns
    for src in user/*.c; do
        name=$(basename "$src" .c)
        x86_64-elf-gcc -c "$src" -o "build/user/$name.o" $flags
        x86_64-elf-gcc -T user/user.ld -o "build/user/$name.elf" -ffreestanding -nostdlib -static -no-pie build/user/crt0.o "build/user/$name.o" build/user/libk.o -lgcc
    done
    gcc -static -no-pie -O2 -Wall -Wextra -o build/habilis/habilis habilis/habilis.c
    for src in compat/primemeridian/tests/*.c; do
        gcc -static -no-pie -O2 -Wall -Wextra -o "build/pm/$(basename "$src" .c)" "$src"
    done
    gcc -static -no-pie -O2 -Wall -Wextra -o build/pm/netconfig network/netconfig.c
    gcc -static -no-pie -O2 -Wall -Wextra -o build/pm/kunix-update system/update.c
    echo 'SELFHOST USERLAND PASS'
    ;;
*) echo 'usage: selfhost-build.sh kernel|userland' >&2; exit 2 ;;
esac
