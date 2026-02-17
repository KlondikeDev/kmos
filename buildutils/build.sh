#!/bin/bash
export PATH="/home/joseph/opt/cross/bin:$PATH"

info(){ printf "\e[1;34m[INFO]\e[0m %s\n" "$*"; }


build(){

    info "Building Kunix..."

    x86_64-elf-as boot/boot.s -o boot.o
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c drivers/serial.c -o serial.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c lib/io.c -o io.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c lib/itoa.c -o itoa.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/idt.c -o idt.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c drivers/pit.c -o pit.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/panic.c -o panic.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c drivers/keyboard.c -o keyboard.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c drivers/pci.c -o pci.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/console.c -o console.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/shell.c -o shell.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c lib/memcpy.c -o memcpy.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c lib/string.c -o string.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c arch/x86/cpuid.c -o cpuid.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/klog.c -o klog.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/ssp.c -o ssp.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
    x86_64-elf-as arch/x86/interrupts.s -o interrupts.o
    x86_64-elf-gcc -Iinclude -Iinclude/klibc -c kernel/kernel.c -o kernel.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fstack-protector-strong
}

link(){
    info "Linking binaries..."
    x86_64-elf-gcc -T buildutils/linker.ld -o kunix-kodiak.elf -ffreestanding -O2 -nostdlib boot.o kernel.o console.o io.o idt.o interrupts.o serial.o pit.o itoa.o panic.o keyboard.o memcpy.o cpuid.o string.o klog.o pci.o shell.o ssp.o -lgcc
    cp kunix-kodiak.elf kunix-kodiak.bin
}

setup(){
    info "Setting up ISO image..."
    mkdir -p isodir/boot/grub
    cp kunix-kodiak.bin isodir/boot/kunix-kodiak.bin
    cp boot/grub.cfg isodir/boot/grub/grub.cfg
    grub-mkrescue -o kunix-kodiak.iso isodir
}

cleaning(){
    info "Cleaning up..."
    mkdir -p build/objects build/binaries
    mv *.o build/objects/
    mv kunix-kodiak.elf build/binaries/
    mv kunix-kodiak.bin build/binaries/
    mv kunix-kodiak.iso build/binaries/
}

qemu(){
    info "Loading in QEMU..."
    qemu-system-x86_64 \
        -cdrom build/binaries/kunix-kodiak.iso \
        -serial stdio \
        -netdev user,id=net0 \
        -device e1000,netdev=net0 \
        -object filter-dump,id=f1,netdev=net0,file=network.pcap
}

main(){
    info "Starting Kunix - Codename Kodiak build..."
    build
    link
    setup
    cleaning
    qemu
}

main "$@"