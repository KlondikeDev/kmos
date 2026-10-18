#!/bin/sh
# This file has been generated in whole or in part by a Large Language Model
# Bootstrap a static Linux/musl hosted C/C++ compiler, binutils and GNU make.
# This is an explicit development kit; it is not part of the normal live root.
set -eu
SELFHOST_DIR=${SELFHOST_DIR:-"$(pwd)/build/selfhost"}
SELFHOST_CACHE=${SELFHOST_CACHE:-"$SELFHOST_DIR/cache"}
mkdir -p "$SELFHOST_CACHE" "$SELFHOST_DIR/toolchain"
fetch() {
    name=$1 url=$2 hash=$3
    if [ ! -f "$SELFHOST_CACHE/$name" ]; then
        curl -fL --retry 3 -o "$SELFHOST_CACHE/$name.part" "$url"
        mv "$SELFHOST_CACHE/$name.part" "$SELFHOST_CACHE/$name"
    fi
    printf '%s  %s\n' "$hash" "$SELFHOST_CACHE/$name" | sha256sum -c -
}
fetch x86_64-linux-musl-native.tgz https://musl.cc/x86_64-linux-musl-native.tgz eb1db6f0f3c2bdbdbfb993d7ef7e2eeef82ac1259f6a6e1757c33a97dbcef3ad
fetch make-4.4.1.tar.gz https://ftp.gnu.org/gnu/make/make-4.4.1.tar.gz dd16fb1d67bfab79a72f5e8390735c49e3e8e70b4945a15ab1f81ddb78658fb3
# Always verify the archives, including an offline rebuild using the cache.
tar -xzf "$SELFHOST_CACHE/x86_64-linux-musl-native.tgz" -C "$SELFHOST_DIR/toolchain" --strip-components=1
# Prefer the existing local source cache; the archive is checked either way.
if [ ! -f "$SELFHOST_CACHE/binutils-2.42.tar.xz" ] && [ -f "$HOME/src/binutils-2.42.tar.xz" ]; then
    cp "$HOME/src/binutils-2.42.tar.xz" "$SELFHOST_CACHE/"
fi
fetch binutils-2.42.tar.xz https://ftp.gnu.org/gnu/binutils/binutils-2.42.tar.xz f6e4d41fd5fc778b06b7891457b3620da5ecea1006c6a4a41ae998109f85a800
mkdir -p "$SELFHOST_DIR/binutils-src"
rm -rf "$SELFHOST_DIR/binutils-build"
mkdir -p "$SELFHOST_DIR/binutils-build"
tar -xJf "$SELFHOST_CACHE/binutils-2.42.tar.xz" -C "$SELFHOST_DIR/binutils-src" --strip-components=1
for tool in ar ranlib nm strip; do
    ln -sf "$tool" "$SELFHOST_DIR/toolchain/bin/x86_64-linux-musl-$tool"
done
# The archive's binutils are static PIE. Kunix currently loads ET_EXEC, so
# rebuild these tools instead of broadening the ELF loader for a bootstrap.
(
    cd "$SELFHOST_DIR/binutils-build"
    export PATH="$SELFHOST_DIR/toolchain/bin:$PATH"
    CC="$SELFHOST_DIR/toolchain/bin/gcc" AR="$SELFHOST_DIR/toolchain/bin/ar" RANLIB="$SELFHOST_DIR/toolchain/bin/ranlib" CFLAGS='-O2 -fno-pie' LDFLAGS='-static -no-pie' ../binutils-src/configure --prefix="$SELFHOST_DIR/toolchain" --target=x86_64-linux-musl --host=x86_64-linux-musl --build=x86_64-pc-linux-gnu --disable-nls --disable-werror --disable-gdb --disable-gprof --disable-gprofng --disable-gold --disable-shared --disable-plugins --without-debuginfod --with-sysroot=/
    make -j"${JOBS:-4}" MAKEINFO=true "CC_FOR_BUILD=$SELFHOST_DIR/toolchain/bin/gcc -static -no-pie" all-binutils all-gas all-ld
    # Libtool interprets -static as static libraries, not a static executable.
    # Its -all-static is invalid for configure's direct GCC link tests. Relink
    # only after configuration/compilation, preserving the already built objects.
    python3 - <<'PYLINK'
from pathlib import Path
for directory in ('gas', 'ld', 'binutils'):
    for path in Path(directory).rglob('*'):
        if path.is_file() and not path.is_symlink():
            with path.open('rb') as binary:
                header = binary.read(18)
            if header[:4] == b'\x7fELF' and int.from_bytes(header[16:18], 'little') in (2, 3):
                path.unlink()
PYLINK
    make -j"${JOBS:-4}" MAKEINFO=true LDFLAGS='-all-static -no-pie' "CC_FOR_BUILD=$SELFHOST_DIR/toolchain/bin/gcc -static -no-pie" all-binutils all-gas all-ld
    make MAKEINFO=true LDFLAGS='-all-static -no-pie' "CC_FOR_BUILD=$SELFHOST_DIR/toolchain/bin/gcc -static -no-pie" install-binutils install-gas install-ld
)
# These optional tools from the bootstrap archive also use unsupported PIE.
rm -f "$SELFHOST_DIR/toolchain/bin/dwp" "$SELFHOST_DIR/toolchain/bin/gprof" "$SELFHOST_DIR/toolchain/bin/ld.gold" "$SELFHOST_DIR/toolchain/x86_64-linux-musl/bin/ld.gold"
rm -f "$SELFHOST_DIR/toolchain/libexec/gcc/x86_64-linux-musl/11.2.1/install-tools/fixincl" "$SELFHOST_DIR/toolchain/libexec/gcc/x86_64-linux-musl/11.2.1/g++-mapper-server"
mkdir -p "$SELFHOST_DIR/make-src"
tar -xzf "$SELFHOST_CACHE/make-4.4.1.tar.gz" -C "$SELFHOST_DIR/make-src" --strip-components=1
(
    cd "$SELFHOST_DIR/make-src"
    CC="$SELFHOST_DIR/toolchain/bin/gcc -static -no-pie" ./configure --prefix=/opt/selfhost --disable-nls --without-guile
    make -j"${JOBS:-4}"
    cp make "$SELFHOST_DIR/toolchain/bin/make"
)
fetch makefs_20190105.orig.tar.gz https://deb.debian.org/debian/pool/main/m/makefs/makefs_20190105.orig.tar.gz 691fe1487944e280c0198cf82ce2c46b91c29f424f1c4dc3c8a4c12e5bc1fd15
mkdir -p "$SELFHOST_DIR/makefs-src"
tar -xzf "$SELFHOST_CACHE/makefs_20190105.orig.tar.gz" -C "$SELFHOST_DIR/makefs-src" --strip-components=1
python3 buildutils/selfhost-makefs.py --source "$SELFHOST_DIR/makefs-src" --cc "$SELFHOST_DIR/toolchain/bin/gcc" --output "$SELFHOST_DIR/toolchain/bin/makefs"
"$SELFHOST_DIR/toolchain/bin/gcc" -static -no-pie -O2 limine/limine.c -o "$SELFHOST_DIR/toolchain/bin/limine"
licenses=$SELFHOST_DIR/toolchain/share/licenses/selfhost
mkdir -p "$licenses"
cp "$SELFHOST_DIR/make-src/COPYING" "$licenses/GNU-GPL-3.0"
cp limine/LICENSE "$licenses/LIMINE-LICENSE"
mkdir -p "$licenses/makefs-source"
cp -R "$SELFHOST_DIR/makefs-src/src/usr.sbin/makefs/." "$licenses/makefs-source/"
cp buildutils/selfhost-makefs-compat/sys/queue.h "$licenses/BSD-QUEUE-LICENSE"
cat > "$licenses/PROVENANCE" <<'EOF'
This file has been generated in whole or in part by a Large Language Model
Bootstrap binary distribution: https://musl.cc/x86_64-linux-musl-native.tgz
SHA256: eb1db6f0f3c2bdbdbfb993d7ef7e2eeef82ac1259f6a6e1757c33a97dbcef3ad
GCC 11.2.1 20211120; bootstrap GCC runtime; musl-hosted static executables.
Publisher/build sources: https://musl.cc/ and https://github.com/richfelker/musl-cross-make
The binary distribution is a bootstrap dependency, not rebuilt from source here.
Its digest records the downloaded artifact, not an upstream signature.
Compiler corresponding source must accompany any public redistribution.
GNU binutils 2.42 rebuilt locally with -static -no-pie.
Source: https://ftp.gnu.org/gnu/binutils/binutils-2.42.tar.xz
SHA256: f6e4d41fd5fc778b06b7891457b3620da5ecea1006c6a4a41ae998109f85a800
GNU make 4.4.1 source: https://ftp.gnu.org/gnu/make/make-4.4.1.tar.gz
SHA256: dd16fb1d67bfab79a72f5e8390735c49e3e8e70b4945a15ab1f81ddb78658fb3
GNU make rebuilt locally with the bootstrap compiler and -static -no-pie.
MirBSD makefs 20190105, musl portability headers in buildutils/selfhost-makefs-compat.
Source: https://deb.debian.org/debian/pool/main/m/makefs/makefs_20190105.orig.tar.gz
SHA256: 691fe1487944e280c0198cf82ce2c46b91c29f424f1c4dc3c8a4c12e5bc1fd15
This product includes software developed for the NetBSD Project by Wasabi Systems, Inc.
Limine host installer rebuilt from the bundled limine.c (see limine/LICENSE).
EOF
"$SELFHOST_DIR/toolchain/bin/gcc" -v 2> "$licenses/GCC-CONFIGURATION"
"$SELFHOST_DIR/toolchain/bin/as" --version > "$licenses/BINUTILS-VERSION"
"$SELFHOST_DIR/toolchain/bin/make" --version > "$licenses/MAKE-VERSION"
echo "Development kit ready: $SELFHOST_DIR/toolchain"
