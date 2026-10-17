#!/usr/bin/env bash
# This file has been generated in whole or in part by a Large Language Model

# Copyright (c) 2026 Joseph Jones
# Licensed under the Kunix License v1.0 or later
# See LICENSE for details

# X for Prime Meridian: the X server (Xfbdev, drawing on /dev/fb0), twm and
# xterm, with the libraries they need, all static against musl.
#
#   buildutils/x11.sh            # everything not yet built
#   buildutils/x11.sh xterm      # one package (again)
#   buildutils/x11.sh list       # the packages, in build order
#
# Needs buildutils/pm-toolchain.sh musl ncurses first. Everything lands in
# $PM_PREFIX/x11, laid out as the Kunix disk will have it (usr/bin,
# usr/share/X11, ...); the build-time headers and libraries live there too.
# build.sh copies the programs and their data onto the disk.
#
# Xfbdev went away in xorg-server 1.20 (only Xephyr is left of kdrive), so
# the server is 1.19.7, the last release that has it.

set -euo pipefail

PM_PREFIX=${PM_PREFIX:-$HOME/opt/primemeridian}
SRC=$PM_PREFIX/src
MUSL=$PM_PREFIX/musl
XROOT=$PM_PREFIX/x11
STAMPS=$XROOT/.built
JOBS=$(nproc)
X=https://www.x.org/releases/individual

mkdir -p "$SRC" "$XROOT/usr" "$STAMPS"

# Built against the tree in $XROOT: its headers, its libraries, its .pc files.
export CC="$MUSL/bin/musl-gcc -static"
export CFLAGS="-O2 -fno-strict-aliasing"
export CPPFLAGS="-I$XROOT/usr/include"
export LDFLAGS="-L$XROOT/usr/lib -static"
export PKG_CONFIG="pkg-config --static"
export PKG_CONFIG_LIBDIR=$XROOT/usr/lib/pkgconfig:$XROOT/usr/share/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=$XROOT
export ACLOCAL_PATH=$XROOT/usr/share/aclocal
# Helpers some builds compile and run on the way (libXt's makestrs, libX11's
# makekeys) are the host's programs: its own gcc and libc.
export CC_FOR_BUILD=gcc CFLAGS_FOR_BUILD=-O2 CPPFLAGS_FOR_BUILD= LDFLAGS_FOR_BUILD=

fetch() {   # url: download once, unpack fresh
    local file=$SRC/${1##*/}
    if [ ! -f "$file" ]; then
        curl -fL --retry 3 -o "$file.part" "$1" || { rm -f "$file.part"; return 1; }
        mv "$file.part" "$file"
    fi
    local dir
    dir=$(tar -tf "$file" | awk -F/ 'NR == 1 { print $1 }')   # (head would kill tar: SIGPIPE)
    rm -rf "${SRC:?}/$dir"
    tar -C "$SRC" -xf "$file"
    cd "$SRC/$dir"
}

# The usual autotools package: configured for /usr, installed into $XROOT.
# (Static musl programs run on the host, so configure's test programs run
# too: no --host, no cross-compiling guesses.)
autotools() {   # configure arguments...
    ./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var \
        --enable-static --disable-shared "$@"
    make -j"$JOBS" ${MAKE_ARGS:+"$MAKE_ARGS"}
    make install DESTDIR="$XROOT" ${MAKE_ARGS:+"$MAKE_ARGS"}
    # libtool archives name /usr/lib, which is the host's: link from the
    # .a files alone.
    find "$XROOT/usr/lib" -name '*.la' -delete
}

PACKAGES="zlib libmd utilmacros xorgproto xtrans libXau xcbproto libxcb libX11 libXext libICE
          libSM libXt libXmu libXpm libXaw libxkbfile libfontenc libXfont2 pixman
          xkbcomp xserver twm xterm data"

build_zlib() {
    fetch https://zlib.net/fossils/zlib-1.3.1.tar.gz
    ./configure --prefix=/usr --static
    make -j"$JOBS"
    make install DESTDIR="$XROOT"
}
build_libmd() {      # SHA-1, for the server (musl has none)
    fetch https://archive.hadrons.org/software/libmd/libmd-1.1.0.tar.xz
    autotools
}
build_utilmacros() { fetch $X/util/util-macros-1.20.1.tar.xz; autotools; }   # xorg-macros.pc (twm asks)
build_xorgproto() { fetch $X/proto/xorgproto-2024.1.tar.xz; autotools; }
build_xtrans()    { fetch $X/lib/xtrans-1.5.0.tar.xz; autotools; }
build_libXau()    { fetch $X/lib/libXau-1.0.11.tar.xz; autotools; }
build_xcbproto()  {
    # Its python module is used by libxcb's build, on the host: install it
    # where its .pc file can say it is.
    fetch $X/xcb/xcb-proto-1.17.0.tar.xz
    ./configure --prefix="$XROOT/usr"
    make install
}
build_libxcb() {
    fetch $X/xcb/libxcb-1.17.0.tar.xz
    autotools --disable-devel-docs --without-doxygen
}
build_libX11() {
    fetch $X/lib/libX11-1.8.10.tar.xz
    autotools --disable-specs --without-xmlto --without-fop --disable-thread-safety-constructor
}
build_libXext()   { fetch $X/lib/libXext-1.3.6.tar.xz; autotools --disable-specs; }
build_libICE()    { fetch $X/lib/libICE-1.1.1.tar.xz; autotools --disable-docs --disable-specs; }
build_libSM()     { fetch $X/lib/libSM-1.2.4.tar.xz; autotools --disable-docs --without-libuuid; }
build_libXt()     { fetch $X/lib/libXt-1.3.0.tar.xz; autotools --disable-specs; }
build_libXmu()    { fetch $X/lib/libXmu-1.2.1.tar.xz; autotools --disable-docs; }
build_libXpm()    { fetch $X/lib/libXpm-3.5.17.tar.xz; autotools --disable-open-zfile; }
build_libXaw()    { fetch $X/lib/libXaw-1.0.16.tar.xz; autotools --disable-specs --disable-xaw6; }
build_libxkbfile(){ fetch $X/lib/libxkbfile-1.1.3.tar.xz; autotools; }
build_libfontenc(){ fetch $X/lib/libfontenc-1.1.8.tar.xz; autotools; }
build_libXfont2() {
    # Bitmap fonts (PCF, gzipped) only: no FreeType.
    fetch $X/lib/libXfont2-2.0.7.tar.xz
    autotools --disable-freetype --disable-devel-docs
}
build_pixman() {
    fetch https://cairographics.org/releases/pixman-0.42.2.tar.gz
    autotools --disable-gtk --disable-libpng --disable-openmp
}
build_xkbcomp() {
    # The server runs it to compile keymaps.
    fetch $X/app/xkbcomp-1.4.7.tar.xz
    autotools --with-xkb-config-root=/usr/share/X11/xkb
}
build_xserver() {
    fetch $X/xserver/xorg-server-1.19.7.tar.bz2
    # glibc's private type names, where the standard ones do.
    sed -i 's/__uid_t/uid_t/; s/__gid_t/gid_t/' hw/kdrive/linux/linux.c
    # The mouse driver picks a protocol before it sets km->tty (is the
    # device a serial line?), reading malloc's leftovers: zero them. glibc's
    # malloc happens to hand back zeroes there; musl's does not, and the
    # mouse ends up taken for a serial one, PS/2 refused.
    sed -i 's/km = (Kmouse \*) malloc(sizeof(Kmouse));/km = (Kmouse *) calloc(1, sizeof(Kmouse));/' hw/kdrive/linux/mouse.c
    grep -q 'calloc(1, sizeof(Kmouse))' hw/kdrive/linux/mouse.c
    # The console keyboard driver never says which scancodes it sends (1.19
    # dropped the code that did), so the server takes every key for out of
    # range. Its codes are XFree86's AT set -- hence the "base" keymap
    # rules below, not "evdev".
    sed -i 's|    ki->name = strdup("Linux console keyboard");|&\n    ki->minScanCode = 0;\n    ki->maxScanCode = 247;|' hw/kdrive/linux/keyboard.c
    grep -q 'ki->maxScanCode = 247;' hw/kdrive/linux/keyboard.c
    # Given a protocol (startx gives ps/2), the mouse driver means to skip
    # its "is this really the protocol?" tests -- so says its comment -- but
    # sets the wrong field (state for stage), and the first packets of
    # every session are thrown away as unproven.
    sed -i 's/            km->state = MouseWorking;/            km->stage = MouseWorking;/' hw/kdrive/linux/mouse.c
    grep -q 'km->stage = MouseWorking;' hw/kdrive/linux/mouse.c
    # Kunix has no threads (so no input thread) and no epoll (the server's
    # poll loop does without it); no GL, DRM, udev, logind or XDMCP.
    # libtool links programs dynamically whatever LDFLAGS says, short of its
    # own -all-static (which configure's test links would not understand).
    MAKE_ARGS="LDFLAGS=$LDFLAGS -all-static" ac_cv_func_epoll_create1=no autotools \
        --enable-kdrive --enable-xfbdev --enable-kdrive-kbd --enable-kdrive-mouse \
        --disable-kdrive-evdev --disable-xephyr --disable-xfake \
        --disable-xorg --disable-xvfb --disable-xnest --disable-xwin --disable-xquartz \
        --disable-xwayland --disable-dmx \
        --disable-glx --disable-dri --disable-dri2 --disable-dri3 --disable-glamor \
        --disable-libdrm --disable-xshmfence --disable-present \
        --disable-config-udev --disable-config-udev-kms --disable-config-hal \
        --disable-systemd-logind --disable-input-thread --disable-libunwind \
        --disable-xdmcp --disable-xdm-auth-1 --disable-secure-rpc --disable-xselinux \
        --disable-docs --disable-devel-docs --disable-unit-tests --without-dtrace \
        --disable-selective-werror \
        --with-sha1=libmd \
        --with-xkb-path=/usr/share/X11/xkb --with-xkb-bin-directory=/usr/bin \
        --with-xkb-output=/tmp --with-default-xkb-rules=base \
        --with-default-font-path=/usr/share/fonts/X11/misc,built-ins \
        --with-fontrootdir=/usr/share/fonts/X11
}
build_twm() {
    fetch $X/app/twm-1.0.12.tar.xz
    autotools
}
build_xterm() {
    fetch https://invisible-island.net/archives/xterm/xterm-390.tgz
    # Bitmap fonts; the terminal library is the musl ncurses' (TERM=xterm
    # is built into it -- see pm-toolchain.sh).
    # It starts the shell's session with setsid() only if _POSIX_SOURCE is
    # defined, which glibc's headers do for everyone and musl's do not
    # (without it, the shell gets no controlling terminal and xterm retries
    # forever). _GNU_SOURCE keeps musl's extensions on regardless.
    CPPFLAGS="$CPPFLAGS -I$MUSL/include/ncursesw -D_GNU_SOURCE -D_POSIX_SOURCE" LDFLAGS="$LDFLAGS -L$MUSL/lib" \
    LIBS="$($PKG_CONFIG --libs xaw7 xmu xt xpm x11 xext sm ice) -ltinfow" \
    autotools --x-includes="$XROOT/usr/include" --x-libraries="$XROOT/usr/lib" \
        --disable-freetype --without-xinerama --disable-setuid --disable-setgid \
        --without-utempter --disable-tek4014 --enable-256-color --with-terminal-type=xterm \
        --with-tty-group=tty --disable-desktop
}

# What the programs read at run time and no package here makes: keymaps
# (the host's xkb-data) and bitmap fonts (the host's xfonts-base: the
# "fixed" family, Latin-1 and Unicode, and the cursors), indexed with
# mkfontdir.
build_data() {
    local fonts=$XROOT/usr/share/fonts/X11/misc
    [ -d /usr/share/X11/xkb ] || { echo "needs the host's xkb data (apt install xkb-data)"; exit 1; }
    [ -d /usr/share/fonts/X11/misc ] || { echo "needs the host's fonts (apt install xfonts-base)"; exit 1; }
    rm -rf "$XROOT/usr/share/X11/xkb" "$fonts"
    cp -rL /usr/share/X11/xkb "$XROOT/usr/share/X11/xkb"
    rm -f "$XROOT"/usr/share/X11/xkb/rules/*.xml            # descriptions for GUIs; large
    mkdir -p "$fonts"
    for f in 6x10 6x13 6x13B 7x13 7x13B 8x13 8x13B 9x15 9x15B 10x20; do
        cp /usr/share/fonts/X11/misc/$f.pcf.gz /usr/share/fonts/X11/misc/$f-ISO8859-1.pcf.gz "$fonts/"
    done
    cp /usr/share/fonts/X11/misc/cursor.pcf.gz /usr/share/fonts/X11/misc/fonts.alias "$fonts/"
    mkfontdir "$fonts"
}

built() { [ -f "$STAMPS/$1" ]; }

build() {   # package
    echo "==== $1"
    # Not `( ... ) || ...`: bash ignores set -e inside anything tested
    # that way, and a failed step would sail on.
    set +e
    ( set -e; "build_$1" ) > "$STAMPS/$1.log" 2>&1
    local rc=$?
    set -e
    if [ $rc -ne 0 ]; then
        echo "$1 failed; the end of $STAMPS/$1.log:"
        tail -30 "$STAMPS/$1.log"
        exit 1
    fi
    touch "$STAMPS/$1"
}

case ${1:-all} in
    all)  for p in $PACKAGES; do built "$p" || build "$p"; done ;;
    list) echo $PACKAGES ;;
    *)    for p in "$@"; do build "$p"; done ;;
esac
