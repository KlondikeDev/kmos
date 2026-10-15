// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// Static ELF64 loader. The file has already been read into kernel memory;
// this checks that it is something we can safely run, then copies each
// PT_LOAD segment to the virtual address it asks for.
//
// Everything in the file is untrusted. Every offset and size is checked
// against the file length, and every address against the user window,
// before anything is dereferenced or mapped.

#include <types.h>
#include <elf.h>
#include <usermode.h>
#include <klog.h>

#define PAGE_MASK (~(USER_PAGE_SIZE - 1))

static bool elf_check_header(const elf64_ehdr_t* eh, u64 size) {
    if (size < sizeof(elf64_ehdr_t)) {
        klog(LOG_ERROR, "elf: file too small for a header (%lu bytes)\n", size);
        return false;
    }
    if (eh->e_ident[0] != ELFMAG0 || eh->e_ident[1] != ELFMAG1 ||
        eh->e_ident[2] != ELFMAG2 || eh->e_ident[3] != ELFMAG3) {
        klog(LOG_ERROR, "elf: bad magic\n");
        return false;
    }
    if (eh->e_ident[EI_CLASS] != ELFCLASS64 || eh->e_ident[EI_DATA] != ELFDATA2LSB ||
        eh->e_ident[EI_VERSION] != EV_CURRENT) {
        klog(LOG_ERROR, "elf: not a little-endian ELF64 file\n");
        return false;
    }
    if (eh->e_type != ET_EXEC) {
        // ET_DYN would need relocation, which we do not do.
        klog(LOG_ERROR, "elf: not a static executable (e_type=%u)\n", eh->e_type);
        return false;
    }
    if (eh->e_machine != EM_X86_64) {
        klog(LOG_ERROR, "elf: not an x86_64 binary (e_machine=%u)\n", eh->e_machine);
        return false;
    }
    if (eh->e_phentsize != sizeof(elf64_phdr_t) || eh->e_phnum == 0) {
        klog(LOG_ERROR, "elf: no usable program headers\n");
        return false;
    }
    u64 table = (u64)eh->e_phnum * sizeof(elf64_phdr_t);
    if (eh->e_phoff > size || table > size - eh->e_phoff) {
        klog(LOG_ERROR, "elf: program header table runs past end of file\n");
        return false;
    }
    return true;
}

static bool elf_check_segment(const elf64_phdr_t* ph, u64 size) {
    if (ph->p_memsz < ph->p_filesz) {
        klog(LOG_ERROR, "elf: segment memsz < filesz\n");
        return false;
    }
    if (ph->p_offset > size || ph->p_filesz > size - ph->p_offset) {
        klog(LOG_ERROR, "elf: segment data runs past end of file\n");
        return false;
    }
    // [vaddr, vaddr + memsz) must sit inside the load window. Written so that
    // a huge memsz cannot wrap around and sneak past the upper bound.
    if (ph->p_vaddr < USER_CODE_BASE || ph->p_vaddr > USER_LOAD_LIMIT ||
        ph->p_memsz > USER_LOAD_LIMIT - ph->p_vaddr) {
        klog(LOG_ERROR, "elf: segment 0x%lx+0x%lx outside user window\n",
             ph->p_vaddr, ph->p_memsz);
        return false;
    }
    return true;
}

bool elf_load(process_t* p, const u8* image, u64 size, u64* entry_out, u64* image_end_out) {
    const elf64_ehdr_t* eh = (const elf64_ehdr_t*)image;
    if (!elf_check_header(eh, size)) return false;

    const elf64_phdr_t* phdrs = (const elf64_phdr_t*)(image + eh->e_phoff);

    // Pass 1: validate everything before mapping anything, so a bad file
    // never leaves a half-built address space behind.
    bool any_load = false;
    bool entry_ok = false;
    for (u16 i = 0; i < eh->e_phnum; i++) {
        const elf64_phdr_t* ph = &phdrs[i];
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;
        if (!elf_check_segment(ph, size)) return false;
        any_load = true;
        if ((ph->p_flags & PF_X) &&
            eh->e_entry >= ph->p_vaddr && eh->e_entry < ph->p_vaddr + ph->p_memsz) {
            entry_ok = true;
        }
    }
    if (!any_load) {
        klog(LOG_ERROR, "elf: no loadable segments\n");
        return false;
    }
    if (!entry_ok) {
        klog(LOG_ERROR, "elf: entry point 0x%lx is not in an executable segment\n", eh->e_entry);
        return false;
    }

    // Pass 2: map and copy. user_map hands back zeroed pages, so the .bss
    // tail (memsz beyond filesz) needs no work. Segments are copied through
    // the kernel's direct map, so p's address space need not be loaded.
    u64 image_end = 0;
    for (u16 i = 0; i < eh->e_phnum; i++) {
        const elf64_phdr_t* ph = &phdrs[i];
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;

        u64 start = ph->p_vaddr & PAGE_MASK;
        u64 end   = (ph->p_vaddr + ph->p_memsz + USER_PAGE_SIZE - 1) & PAGE_MASK;
        for (u64 page = start; page < end; page += USER_PAGE_SIZE) {
            if (!user_map(p, page, ph->p_flags & (PF_R | PF_W | PF_X))) return false;
        }
        if (ph->p_filesz &&
            !user_copy_to(p, ph->p_vaddr, image + ph->p_offset, ph->p_filesz)) {
            return false;
        }
        if (end > image_end) image_end = end;

        char perms[4] = {
            (ph->p_flags & PF_R) ? 'r' : '-',
            (ph->p_flags & PF_W) ? 'w' : '-',
            (ph->p_flags & PF_X) ? 'x' : '-',
            '\0',
        };
        klog(LOG_DEBUG, "elf: segment 0x%lx-0x%lx %s (%lu bytes from file)\n",
             ph->p_vaddr, ph->p_vaddr + ph->p_memsz, perms, ph->p_filesz);
    }

    *image_end_out = image_end;
    *entry_out = eh->e_entry;
    return true;
}
