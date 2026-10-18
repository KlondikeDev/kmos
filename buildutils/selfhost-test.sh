#!/bin/sh
# This file has been generated in whole or in part by a Large Language Model
# Actual guest compilation, including the complete kernel and native userland.
set -u
exec >/dev/serial 2>&1
export PATH=/opt/selfhost/bin:$PATH
cd /usr/src/kunix || exit 1
passed=0
failed=0
check() {
    label=$1
    shift
    if "$@"; then
        echo "PASS $label"
        passed=$((passed + 1))
    else
        echo "FAIL $label"
        failed=$((failed + 1))
    fi
}
mkdir -p build/selfhost
cat > build/selfhost/hello.c <<'EOF'
#include <stdio.h>
int main(void) { puts("selfhost: compiled musl program executed"); return 0; }
EOF
compile_run() {
    gcc -static -no-pie -O2 -o build/selfhost/hello build/selfhost/hello.c && build/selfhost/hello
}
check 'musl C compile, assemble, link and execute' compile_run
if [ "$failed" -eq 0 ]; then
    check 'complete Kunix kernel from source' make -f buildutils/selfhost.mk kernel
    check 'native userland, Habilis and Prime Meridian programs from source' make -f buildutils/selfhost.mk userland
fi
if [ "$failed" -eq 0 ] && [ "${1:-}" = image ]; then
    export SELFHOST_IMAGE_DIR=/var/tmp/kunix-selfhost-iso
    export SELFHOST_IMAGE_OUTPUT=/var/tmp/kunix-selfhost.iso
    check 'new UFS2 root and bootable BIOS ISO' make -f buildutils/selfhost.mk image
    if [ "$failed" -eq 0 ]; then
        # This mode is only for test.py's explicitly attached disposable NVMe.
        # Never use it on a physical machine: disk8 is the dedicated test output.
        check 'export ISO to disposable QEMU NVMe' dd if="$SELFHOST_IMAGE_OUTPUT" of=/dev/disk8 bs=1M conv=fsync
        sha256sum "$SELFHOST_IMAGE_OUTPUT"
        wc -c < "$SELFHOST_IMAGE_OUTPUT"
    fi
fi
echo "selfhost: $passed passed, $failed failed"
[ "$failed" -eq 0 ]
