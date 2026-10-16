// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// Static ELF64 loader. Reads the headers, checks that the file is something
// we can safely run, then reads each PT_LOAD segment from the file straight
// into the frames backing the virtual addresses it asks for.
//
// Its complaints go to the serial port only: programs probe with execve and
// fall back (a shell running a script), so a file that is not an
// executable is ordinary, not an error.
//
// Everything in the file is untrusted. Every offset and size is checked
// against the file length, and every address against the user window,
// before anything is dereferenced or mapped.

#include <types.h>
#include <elf.h>
#include <usermode.h>
#include <klog.h>
#include <serial.h>
#include <memory.h>
#include <errno.h>

#define PAGE_MASK (~(USER_PAGE_SIZE - 1))

static bool elf_check_header(const elf64_ehdr_t* eh, u64 size) {
    if (size < sizeof(elf64_ehdr_t)) {
        kserialoutf("elf: file too small for a header (%lu bytes)\n", size);
        return false;
    }
    if (eh->e_ident[0] != ELFMAG0 || eh->e_ident[1] != ELFMAG1 ||
        eh->e_ident[2] != ELFMAG2 || eh->e_ident[3] != ELFMAG3) {
        kserialoutf("elf: bad magic\n");
        return false;
    }
    if (eh->e_ident[EI_CLASS] != ELFCLASS64 || eh->e_ident[EI_DATA] != ELFDATA2LSB ||
        eh->e_ident[EI_VERSION] != EV_CURRENT) {
        kserialoutf("elf: not a little-endian ELF64 file\n");
        return false;
    }
    if (eh->e_type != ET_EXEC) {
        // ET_DYN would need relocation, which we do not do.
        kserialoutf("elf: not a static executable (e_type=%u)\n", eh->e_type);
        return false;
    }
    if (eh->e_machine != EM_X86_64) {
        kserialoutf("elf: not an x86_64 binary (e_machine=%u)\n", eh->e_machine);
        return false;
    }
    if (eh->e_phentsize != sizeof(elf64_phdr_t) || eh->e_phnum == 0) {
        kserialoutf("elf: no usable program headers\n");
        return false;
    }
    u64 table = (u64)eh->e_phnum * sizeof(elf64_phdr_t);
    if (eh->e_phoff > size || table > size - eh->e_phoff) {
        kserialoutf("elf: program header table runs past end of file\n");
        return false;
    }
    return true;
}

static bool elf_check_segment(const elf64_phdr_t* ph, u64 size) {
    if (ph->p_memsz < ph->p_filesz) {
        kserialoutf("elf: segment memsz < filesz\n");
        return false;
    }
    if (ph->p_offset > size || ph->p_filesz > size - ph->p_offset) {
        kserialoutf("elf: segment data runs past end of file\n");
        return false;
    }
    // [vaddr, vaddr + memsz) must sit inside the load window. Written so that
    // a huge memsz cannot wrap around and sneak past the upper bound.
    if (ph->p_vaddr < USER_CODE_BASE || ph->p_vaddr > USER_LOAD_LIMIT ||
        ph->p_memsz > USER_LOAD_LIMIT - ph->p_vaddr) {
        kserialoutf("elf: segment 0x%lx+0x%lx outside user window\n",
             ph->p_vaddr, ph->p_memsz);
        return false;
    }
    return true;
}

#define ELF_MAX_PHDRS 64

// Read file bytes into p's (already mapped) pages, through the direct map.
static bool read_into(process_t* p, vnode_t* node, u64 off, u64 virt, u64 len) {
    while (len) {
        u64 pte = vmm_get_pte(p->space, virt);
        if (!(pte & PTE_PRESENT)) return false;
        u64 in_page = virt & (USER_PAGE_SIZE - 1);
        u64 chunk = USER_PAGE_SIZE - in_page;
        if (chunk > len) chunk = len;
        void* dst = (void*)((pte & 0x000FFFFFFFFFF000ULL) + vmm_hhdm() + in_page);
        if (vfs_read(node, off, dst, chunk) != (i64)chunk) return false;
        virt += chunk;
        off += chunk;
        len -= chunk;
    }
    return true;
}

i32 elf_load(process_t* p, vnode_t* node, elf_info_t* out) {
    u64 size = node->size;
    elf64_ehdr_t eh;
    if (size < sizeof(eh) || vfs_read(node, 0, &eh, sizeof(eh)) != (i64)sizeof(eh)) {
        kserialoutf("elf: file too small for a header (%lu bytes)\n", size);
        return -ENOEXEC;
    }
    if (!elf_check_header(&eh, size)) return -ENOEXEC;
    if (eh.e_phnum > ELF_MAX_PHDRS) {
        kserialoutf("elf: %u program headers is too many\n", eh.e_phnum);
        return -ENOEXEC;
    }

    u64 table = (u64)eh.e_phnum * sizeof(elf64_phdr_t);
    elf64_phdr_t* phdrs = kmalloc(table);
    if (!phdrs) return -ENOMEM;
    i32 err = -ENOEXEC;
    if (vfs_read(node, eh.e_phoff, phdrs, table) != (i64)table) goto out;

    // Pass 1: validate everything before mapping anything, so a bad file
    // never leaves a half-built address space behind.
    bool any_load = false;
    bool entry_ok = false;
    u64 phdr_addr = 0;
    for (u16 i = 0; i < eh.e_phnum; i++) {
        const elf64_phdr_t* ph = &phdrs[i];
        if (ph->p_type == PT_INTERP) {
            kserialoutf("elf: dynamically linked; only static programs run so far\n");
            goto out;
        }
        if (ph->p_type == PT_PHDR) phdr_addr = ph->p_vaddr;
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;
        if (!elf_check_segment(ph, size)) goto out;
        any_load = true;
        if ((ph->p_flags & PF_X) &&
            eh.e_entry >= ph->p_vaddr && eh.e_entry < ph->p_vaddr + ph->p_memsz) {
            entry_ok = true;
        }
        // Without a PT_PHDR, the headers are wherever the segment holding
        // that part of the file lands.
        if (!phdr_addr && eh.e_phoff >= ph->p_offset &&
            eh.e_phoff + table <= ph->p_offset + ph->p_filesz) {
            phdr_addr = ph->p_vaddr + (eh.e_phoff - ph->p_offset);
        }
    }
    if (!any_load) {
        kserialoutf("elf: no loadable segments\n");
        goto out;
    }
    if (!entry_ok) {
        kserialoutf("elf: entry point 0x%lx is not in an executable segment\n", eh.e_entry);
        goto out;
    }

    // Pass 2: map and fill. user_map hands back zeroed pages, so the .bss
    // tail (memsz beyond filesz) needs no work. Segments are written through
    // the kernel's direct map, so p's address space need not be loaded.
    u64 image_end = 0;
    err = -ENOMEM;
    for (u16 i = 0; i < eh.e_phnum; i++) {
        const elf64_phdr_t* ph = &phdrs[i];
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;

        u64 start = ph->p_vaddr & PAGE_MASK;
        u64 end   = (ph->p_vaddr + ph->p_memsz + USER_PAGE_SIZE - 1) & PAGE_MASK;
        for (u64 page = start; page < end; page += USER_PAGE_SIZE) {
            if (!user_map(p, page, ph->p_flags & (PF_R | PF_W | PF_X))) goto out;
        }
        if (ph->p_filesz && !read_into(p, node, ph->p_offset, ph->p_vaddr, ph->p_filesz)) {
            err = -EIO;
            goto out;
        }
        if (end > image_end) image_end = end;

        char perms[4] = {
            (ph->p_flags & PF_R) ? 'r' : '-',
            (ph->p_flags & PF_W) ? 'w' : '-',
            (ph->p_flags & PF_X) ? 'x' : '-',
            '\0',
        };
        // Serial only: one line per segment per exec would bury the console.
        kserialoutf("elf: segment 0x%lx-0x%lx %s (%lu bytes from file)\n",
             ph->p_vaddr, ph->p_vaddr + ph->p_memsz, perms, ph->p_filesz);
    }

    out->entry = eh.e_entry;
    out->image_end = image_end;
    out->phdr = phdr_addr;
    out->phnum = eh.e_phnum;
    err = 0;
out:
    kfree(phdrs);
    return err;
}
