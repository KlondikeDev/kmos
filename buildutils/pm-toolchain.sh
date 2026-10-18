#!/usr/bin/env bash
# This file has been generated in whole or in part by a Large Language Model

# Copyright (c) 2026 Joseph Jones
# Licensed under the Kunix License v1.0 or later
# See LICENSE for details

# Prime Meridian userland: build static Linux x86_64 programs against musl,
# to run unmodified on Kunix through the Linux compatibility layer.
#
#   buildutils/pm-toolchain.sh musl      # musl libc + the musl-gcc wrapper
#   buildutils/pm-toolchain.sh ncurses   # terminfo/termcap, for zsh
#   buildutils/pm-toolchain.sh zsh
#   buildutils/pm-toolchain.sh dropbear  # SSH server/client/key tool (no autostart)
#   buildutils/pm-toolchain.sh busybox
#   buildutils/pm-toolchain.sh all
#
# Uses the host's gcc; nothing is installed system-wide. Everything lands in
# $PM_PREFIX (default ~/opt/primemeridian): sources in src/, the musl sysroot
# in musl/, finished binaries in bin/. build.sh copies bin/ onto the disk.

set -euo pipefail

PM_PREFIX=${PM_PREFIX:-$HOME/opt/primemeridian}
SRC=$PM_PREFIX/src
MUSL=${PM_MUSL_PREFIX:-$PM_PREFIX/musl}
OUT=$PM_PREFIX/bin
JOBS=$(nproc)

MUSL_VER=1.2.5
NCURSES_VER=6.5
ZSH_VER=5.9.2
BUSYBOX_VER=1.36.1
DROPBEAR_VER=2026.94

mkdir -p "$SRC" "$OUT"

fetch() {   # url
    local file=$SRC/${1##*/}
    if [ ! -f "$file" ]; then
        curl -fL --retry 3 -o "$file.part" "$1" || { rm -f "$file.part"; return 1; }
        mv "$file.part" "$file"
    fi
    if [ -n "${2:-}" ]; then
        printf '%s  %s\n' "$2" "$file" | sha256sum -c -
    fi
    # Fresh every time: unpacked over an old build, make finds yesterday's
    # generated files up to date (ncurses' built-in terminals, say).
    local dir
    dir=$(tar -tf "$file" | awk -F/ 'NR == 1 { print $1 }')
    rm -rf "${SRC:?}/$dir"
    tar -C "$SRC" -xf "$file"
}

build_musl() {
    fetch https://musl.libc.org/releases/musl-$MUSL_VER.tar.gz
    cd "$SRC/musl-$MUSL_VER"
    ./configure --prefix="$MUSL" --disable-shared --syslibdir="$MUSL/lib"
    make -j"$JOBS"
    make install
    # musl-gcc only wraps the C library. Programs that include Linux kernel
    # headers (busybox does) need those too: the host's will do.
    mkdir -p "$MUSL/include"
    for d in linux asm asm-generic mtd; do
        src=/usr/include/$d
        [ -d "$src" ] || src=/usr/include/x86_64-linux-gnu/$d
        [ -d "$src" ] && cp -rL "$src" "$MUSL/include/"
    done
}

# Always static: there is no dynamic linker on Kunix (or a shared libc).
CC_MUSL() { echo "$MUSL/bin/musl-gcc -static"; }

build_ncurses() {
    fetch https://ftp.gnu.org/gnu/ncurses/ncurses-$NCURSES_VER.tar.gz
    cd "$SRC/ncurses-$NCURSES_VER"
    # The terminal descriptions are compiled into the library, so nothing
    # needs a terminfo database on the Kunix disk: the console's (linux)
    # and xterm's.
    CC="$(CC_MUSL)" ./configure --prefix="$MUSL" --without-shared --without-cxx \
        --without-cxx-binding --without-ada --without-tests --without-manpages \
        --without-progs --disable-database --with-fallbacks=linux,vt100,dumb,xterm,xterm-256color \
        --enable-termcap --with-termlib
    make -j"$JOBS"
    make install
}

build_zsh() {
    # zsh.org keeps only the newest release in /pub; older ones move to old/.
    fetch https://www.zsh.org/pub/zsh-$ZSH_VER.tar.xz ||
        fetch https://www.zsh.org/pub/old/zsh-$ZSH_VER.tar.xz
    cd "$SRC/zsh-$ZSH_VER"
    # Modules are linked in (no dynamic loading). ncurses was built wide:
    # its libraries carry a "w".
    CC="$(CC_MUSL)" CPPFLAGS="-I$MUSL/include/ncursesw" LDFLAGS="-L$MUSL/lib" \
        ./configure --prefix=/ --host=x86_64-linux-musl \
        --disable-dynamic --disable-gdbm --disable-pcre --disable-cap \
        --with-tcsetpgrp --with-term-lib="tinfow ncursesw" \
        --enable-etcdir=/etc --enable-zshenv=/etc/zshenv
    make -j"$JOBS"
    strip -o "$OUT/zsh" Src/zsh
}

build_busybox() {
    fetch https://busybox.net/downloads/busybox-$BUSYBOX_VER.tar.bz2
    cd "$SRC/busybox-$BUSYBOX_VER"
    make defconfig
    # Static, and without the applets that need kernel features Kunix does
    # not have (networking, mounts, users, modules).
    sed -i -e 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' \
           -e 's/^CONFIG_TC=y/# CONFIG_TC is not set/' .config
    make -j"$JOBS" CC="$(CC_MUSL)" HOSTCC=gcc
    strip -o "$OUT/busybox" busybox
}

build_dropbear() {
    fetch https://matt.ucc.asn.au/dropbear/releases/dropbear-$DROPBEAR_VER.tar.bz2 e098034a843699200c8c977a991fff73159735bf795d5f72ef672c41a6b1ae81
    cd "$SRC/dropbear-$DROPBEAR_VER"
    # Keep public-key/password authentication and PTYs. Kunix does not supply
    # login accounting files; linking bundled crypto avoids host dependencies.
    CC="$(CC_MUSL)" ./configure --host=x86_64-linux-musl --prefix=/ \
        --enable-static --enable-bundled-libtom --disable-zlib --disable-syslog \
        --disable-lastlog --disable-utmp --disable-utmpx --disable-wtmp \
        --disable-wtmpx --disable-loginfunc --disable-pututline --disable-pututxline
    make -j"$JOBS" PROGRAMS="dropbear dbclient dropbearkey"
    for binary in dropbear dbclient dropbearkey; do
        strip -o "$OUT/$binary" "$binary"
    done
    mkdir -p "$PM_PREFIX/share/licenses/dropbear"
    cp LICENSE "$PM_PREFIX/share/licenses/dropbear/LICENSE"
    {
        echo 'This file has been generated in whole or in part by a Large Language Model'
        echo "Dropbear $DROPBEAR_VER"
        echo "Source: https://matt.ucc.asn.au/dropbear/releases/dropbear-$DROPBEAR_VER.tar.bz2"
        echo "Observed archive SHA256 (provenance, not an upstream signature):"
        sha256sum "$SRC/dropbear-$DROPBEAR_VER.tar.bz2"
        echo 'Static musl, bundled libtomcrypt/libtommath, no zlib/syslog/login accounting.'
        echo 'Authentication, user privilege changes and PTYs retain upstream defaults.'
    } > "$PM_PREFIX/share/licenses/dropbear/PROVENANCE"
}

case ${1:-all} in
    musl)    build_musl ;;
    ncurses) build_ncurses ;;
    zsh)     build_zsh ;;
    busybox) build_busybox ;;
    dropbear) build_dropbear ;;
    all)     build_musl; build_ncurses; build_zsh; build_busybox; build_dropbear ;;
    *)       echo "usage: $0 [musl|ncurses|zsh|busybox|dropbear|all]" >&2; exit 2 ;;
esac
