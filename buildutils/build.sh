#!/bin/bash
export PATH="/home/joseph/opt/cross/bin:$PATH"

info(){ printf "\e[1;34m[INFO]\e[0m %s\n" "$*"; }


build(){

    info "Building Kunix..."

    x86_64-elf-as boot/entry.s -o boot.o
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c drivers/serial.c -o serial.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c lib/io.c -o io.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c lib/itoa.c -o itoa.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/idt.c -o idt.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c drivers/pit.c -o pit.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/panic.c -o panic.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c drivers/keyboard.c -o keyboard.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c drivers/ata.c -o ata.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c fs/fat32.c -o fat32.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c fs/vfs.c -o vfs.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c fs/ufs2.c -o ufs2.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c drivers/pci.c -o pci.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/console.c -o console.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/shell.c -o shell.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c lib/memcpy.c -o memcpy.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c lib/string.c -o string.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c arch/x86/cpuid.c -o cpuid.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c arch/x86/cpuinit.c -o cpuinit.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/klog.c -o klog.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/security.c -o security.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c lib/memset.c -o memset.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/memory.c -o memory.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/gdt.c -o gdt.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/usermode.c -o usermode.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/elf.c -o elf.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/proc.c -o proc.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/file.c -o file.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/signal.c -o signal.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/syscall.c -o syscall.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c drivers/tty.c -o tty.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c drivers/rtc.c -o rtc.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/mman.c -o mman.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c drivers/vt.c -o vt.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c drivers/pty.c -o pty.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/socket.c -o socket.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c compat/primemeridian/socket.c -o pm_socket.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c fs/procfs.c -o procfs.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c fs/tmpfs.c -o tmpfs.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/time.c -o time.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/power.c -o power.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c lib/ksnprintf.c -o ksnprintf.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c compat/primemeridian/syscall.c -o pm_syscall.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c compat/primemeridian/signal.c -o pm_signal.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c fs/devfs.c -o devfs.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-as arch/x86/usermode.s -o usermode_asm.o
    x86_64-elf-as compat/primemeridian/entry.s -o pm_entry.o
    x86_64-elf-as arch/x86/switch.s -o switch.o
    x86_64-elf-as arch/x86/interrupts.s -o interrupts.o
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/kernel.c -o kernel.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
}

link(){
    info "Linking binaries..."
    x86_64-elf-gcc -T buildutils/linker.ld -o kunix-kodiak.elf -ffreestanding -O2 -nostdlib boot.o kernel.o console.o io.o idt.o interrupts.o serial.o pit.o itoa.o panic.o keyboard.o memcpy.o cpuid.o cpuinit.o string.o klog.o pci.o shell.o security.o memset.o memory.o ata.o fat32.o gdt.o usermode.o usermode_asm.o elf.o proc.o switch.o vfs.o ufs2.o file.o signal.o syscall.o tty.o devfs.o rtc.o mman.o vt.o pty.o socket.o pm_socket.o procfs.o tmpfs.o time.o power.o ksnprintf.o pm_syscall.o pm_signal.o pm_entry.o -lgcc
    cp kunix-kodiak.elf kunix-kodiak.bin
}

# Ring 3 programs: each user/<name>.c becomes a static ELF linked against
# libk (user/lib) and installed on the FAT32 disk as /BIN/<NAME>.ELF.
# Userland may use SSE -- the kernel saves FPU state on every context switch.
# No stack protector (libk has no __stack_chk_guard yet). libk itself is
# built with -fno-builtin so gcc cannot turn memset's loop into a call to
# memset.
userland(){
    info "Building userland..."
    mkdir -p build/user
    UFLAGS="-std=gnu99 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pic -Iuser"
    x86_64-elf-as user/crt0.s -o build/user/crt0.o
    x86_64-elf-gcc -c user/lib/libk.c -o build/user/libk.o $UFLAGS -fno-builtin -fno-tree-loop-distribute-patterns
    # Only create /BIN if it is missing: on a name clash mmd stops and asks
    # what to do, which hangs an interactive build forever.
    mdir -i disk.img ::/BIN >/dev/null 2>&1 || mmd -i disk.img ::/BIN
    for src in user/*.c; do
        name=$(basename "$src" .c)
        x86_64-elf-gcc -c "$src" -o "build/user/$name.o" $UFLAGS
        x86_64-elf-gcc -T user/user.ld -o "build/user/$name.elf" -ffreestanding -nostdlib -static -no-pie build/user/crt0.o "build/user/$name.o" build/user/libk.o -lgcc
        mcopy -o -i disk.img "build/user/$name.elf" "::/BIN/$(echo "$name" | tr a-z A-Z).ELF"
    done
}

# Prime Meridian: Linux programs, built static against musl on the host
# (buildutils/pm-toolchain.sh sets that up). They go on the UFS2 disk, which
# has room and long names: /ufs/bin/<name>. Skipped if musl is not built.
primemeridian(){
    PM_PREFIX=${PM_PREFIX:-$HOME/opt/primemeridian}
    MUSL_GCC=$PM_PREFIX/musl/bin/musl-gcc
    rm -rf build/pm
    mkdir -p build/pm
    if [ ! -x "$MUSL_GCC" ]; then
        info "musl not built; skipping Linux programs (buildutils/pm-toolchain.sh musl)"
        return
    fi
    info "Building Prime Meridian test programs..."
    for src in compat/primemeridian/tests/*.c; do
        "$MUSL_GCC" -static -O2 -Wall -Wextra -o "build/pm/$(basename "$src" .c)" "$src"
    done
    # Habilis, the init system: an ordinary static program too.
    mkdir -p build/habilis
    "$MUSL_GCC" -static -O2 -Wall -Wextra -o build/habilis/habilis habilis/habilis.c
    # ...and the test scripts (#! zsh), installed without their extension.
    for src in compat/primemeridian/tests/*.zsh; do
        install -m 755 "$src" "build/pm/$(basename "$src" .zsh)"
    done
    # Ported software (zsh, busybox, ...), if it has been built.
    [ -d "$PM_PREFIX/bin" ] && cp "$PM_PREFIX"/bin/* build/pm/ 2>/dev/null
    true
}

setup(){
    info "Setting up ISO image..."
    mkdir -p iso_root/boot/limine
    mkdir -p build/binaries
    cp kunix-kodiak.elf iso_root/boot/
    cp boot/limine.conf iso_root/boot/limine/
    cp limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso_root/boot/limine/
    xorriso -as mkisofs \
        -b boot/limine/limine-bios-cd.bin \
        -no-emul-boot -boot-load-size 4 -boot-info-table \
        --efi-boot boot/limine/limine-uefi-cd.bin \
        -efi-boot-part --efi-boot-image \
        -o build/binaries/kunix-kodiak.iso iso_root
    ./limine/limine bios-install build/binaries/kunix-kodiak.iso
}
cleaning(){
    info "Cleaning up..."
    mkdir -p build/objects build/binaries
    mv *.o build/objects/
    mv kunix-kodiak.elf build/binaries/
    mv kunix-kodiak.bin build/binaries/
}
disk(){
    # Only create + format if the image is missing, so files written to
    # an existing disk survive rebuilds (never reformat a live disk).
    if [ ! -f disk.img ]; then
        info "Creating FAT32 disk image..."
        dd if=/dev/zero of=disk.img bs=1M count=64 status=none
        /usr/sbin/mkfs.fat -F 32 disk.img
    fi
}

# The root filesystem: UFS2, on the second disk (primary slave). Rebuilt
# from scratch every time from the tree -- /bin, /etc and the test data in
# /test -- so anything written to it on Kunix is gone after the next build.
# What should last belongs in /home (below). Needs NetBSD's makefs (Debian
# package "makefs").
ufs(){
    if ! command -v makefs >/dev/null; then
        info "makefs not installed; skipping ufs.img (sudo apt install makefs)"
        return
    fi
    info "Building the UFS2 root image..."
    rm -rf build/ufsroot
    mkdir -p build/ufsroot/bin build/ufsroot/sbin build/ufsroot/etc build/ufsroot/test/docs/deeper
    cp build/user/*.elf build/ufsroot/bin/
    cp build/pm/* build/ufsroot/bin/ 2>/dev/null || true
    # busybox decides what to be from argv[0]: give it every applet name,
    # as hard links (UFS2 keeps them; they cost no inodes). Its "sh" (ash)
    # is /bin/sh.
    if [ -x build/ufsroot/bin/busybox ]; then
        for applet in $(build/ufsroot/bin/busybox --list); do
            [ -e "build/ufsroot/bin/$applet" ] || ln build/ufsroot/bin/busybox "build/ufsroot/bin/$applet"
        done
    fi
    # Kunix's own programs, by their plain names too -- unless busybox
    # already has a (fuller) tool of that name.
    for elf in build/user/*.elf; do
        name=$(basename "$elf" .elf)
        [ -e "build/ufsroot/bin/$name" ] || cp "$elf" "build/ufsroot/bin/$name"
    done
    cp -r etc/. build/ufsroot/etc/
    # Habilis is /sbin/init (the kernel's first choice) and /sbin/habilis.
    if [ -x build/habilis/habilis ]; then
        cp build/habilis/habilis build/ufsroot/sbin/init
        ln build/ufsroot/sbin/init build/ufsroot/sbin/habilis
    fi
    T=build/ufsroot/test
    printf 'Hello from UFS2!\nThis file lives on the second disk.\n' > $T/hello.txt
    printf 'Nested three levels down.\n' > $T/docs/deeper/nested.txt
    printf 'A file whose name is far too long for FAT 8.3.\n' > $T/docs/a-rather-long-file-name.txt
    # Big enough to need indirect blocks: every line numbered, so a reader
    # can check nothing was skipped or repeated.
    seq -f "line %06g of the big file" 1 110000 > $T/big.txt
    chmod -R go-w build/ufsroot                         # whatever the host's umask was
    # Pristine account files, for the accounts service to seed from.
    mkdir -p build/ufsroot/etc/defaults
    cp etc/passwd etc/group etc/shadow build/ufsroot/etc/defaults/
    chmod 600 build/ufsroot/etc/shadow build/ufsroot/etc/defaults/shadow   # root's eyes only
    chmod 644 build/ufsroot/etc/defaults/passwd build/ufsroot/etc/defaults/group
    # busybox is set-user-ID root, for su, passwd and login; it drops the
    # privilege at once for every applet that does not need it.
    [ -f build/ufsroot/bin/busybox ] && chmod 4755 build/ufsroot/bin/busybox
    rm -f ufs.img
    # Room to write in: 64 MiB, and at least 8192 inodes free (makefs would
    # otherwise size the inode table to exactly what is on the image).
    makefs -t ffs -o version=2 -s 64m -f 8192 ufs.img build/ufsroot > /dev/null
    python3 buildutils/ufschown.py ufs.img 0 0          # everything is root's
}

# Home directories: a UFS2 disk of their own (secondary slave), mounted at
# /home. Made once, then never rebuilt or touched by the build -- it is
# yours. (Delete home.img to start over.)
home(){
    [ -f home.img ] && return
    command -v makefs >/dev/null || return
    info "Creating the home disk (home.img)..."
    rm -rf build/homeroot
    mkdir -p build/homeroot/root
    chmod 700 build/homeroot/root
    makefs -t ffs -o version=2 -s 128m -f 16384 home.img build/homeroot > /dev/null
    python3 buildutils/ufschown.py home.img 0 0
}

qemu(){
    info "Loading in QEMU..."
    qemu-system-x86_64 \
        -cdrom build/binaries/kunix-kodiak.iso \
        -boot d \
        -serial stdio \
        -netdev user,id=net0 \
        -device e1000,netdev=net0 \
        -object filter-dump,id=f1,netdev=net0,file=network.pcap \
        -drive file=disk.img,format=raw,if=ide,index=0 \
        $( [ -f ufs.img ] && echo "-drive file=ufs.img,format=raw,if=ide,index=1" ) \
        $( [ -f home.img ] && echo "-drive file=home.img,format=raw,if=ide,index=3" )
}

main(){
    info "Starting Kunix - Codename Kodiak build..."
    disk
    build
    link
    userland
    primemeridian
    ufs
    home
    setup
    cleaning
    qemu
}

main "$@"