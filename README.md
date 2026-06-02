# Kunix Millennium Operating System

![License](https://img.shields.io/badge/license-KPL-blue)
![Language](https://img.shields.io/badge/language-C-lightgrey)
![Arch](https://img.shields.io/badge/arch-x86__64-informational)
![Status](https://img.shields.io/badge/status-pre--userspace-yellow)
![Bootloader](https://img.shields.io/badge/bootloader-Limine-green)

KMOS (Kunix Millennium Operating System), codename **Kodiak**, is a from-scratch monolithic kernel (designed to eventually be Unix-like) for x86_64, developed under the [Klondike Software Project](https://kunix.org). It is written in C and licensed under the Kunix License.

---

## Status

KMOS is in early kernel development. Userspace has not yet been reached.

**Implemented:**

- Limine bootloader integration
- IDT and PIT
- Serial and PS/2 keyboard drivers
- Framebuffer console
- PCI enumeration
- Bitmap physical memory manager
- Four-level page table VMM
- Heap allocator (`kmalloc` / `kfree`)
- Kernel shell

**In progress:**

- Scheduler
- Syscall interface
- Ring 3 / userspace transition

**Planned:**

- UFS2 filesystem support
- ELF loader
- POSIX compatibility layer

---

## Building

Requirements: `gcc`, `binutils`, `make`, `xorriso`, `nasm`

```sh
make
```

Output ISO is placed in `build/`.

---

## Running

```sh
qemu-system-x86_64 -cdrom build/kmos.iso -serial stdio
```

---

## License

Copyright (C) Joseph Jones. Distributed under the Kunix License. See `LICENSE`.
