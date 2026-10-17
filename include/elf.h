// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>
#include <proc.h>
#include <vfs.h>

// ELF64 on-disk structures, as laid out by the System V ABI. Only the parts a
// static executable loader needs are here: the file header and the program
// headers. Section headers are a linker concern and are never looked at.

#define EI_NIDENT 16

typedef struct PACKED {
    u8  e_ident[EI_NIDENT];  // magic, class, endianness, version, ABI
    u16 e_type;              // ET_EXEC for a static executable
    u16 e_machine;           // EM_X86_64
    u32 e_version;
    u64 e_entry;             // virtual address of the first instruction
    u64 e_phoff;             // file offset of the program header table
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;         // size of one program header
    u16 e_phnum;             // number of program headers
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} elf64_ehdr_t;

typedef struct PACKED {
    u32 p_type;              // PT_LOAD is the only one we act on
    u32 p_flags;             // PF_R / PF_W / PF_X
    u64 p_offset;            // where the segment's bytes start in the file
    u64 p_vaddr;             // where they go in memory
    u64 p_paddr;
    u64 p_filesz;            // bytes present in the file
    u64 p_memsz;             // bytes in memory; the excess is zeroed (.bss)
    u64 p_align;
} elf64_phdr_t;

_Static_assert(sizeof(elf64_ehdr_t) == 64, "elf64_ehdr_t must be 64 bytes");
_Static_assert(sizeof(elf64_phdr_t) == 56, "elf64_phdr_t must be 56 bytes");

#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define EI_CLASS   4
#define EI_DATA    5
#define EI_VERSION 6

#define ELFCLASS64  2
#define ELFDATA2LSB 1
#define EV_CURRENT  1

#define ET_EXEC   2
#define EM_X86_64 62

#define PT_NULL   0
#define PT_LOAD   1
#define PT_INTERP 3
#define PT_PHDR   6

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

// What the loader learned, for the entry frame and the auxiliary vector.
typedef struct {
    u64 entry;
    u64 image_end;   // page-aligned end of the image: where the heap starts
    u64 phdr;        // where the program headers are, in the loaded image
    u64 phnum;
} elf_info_t;

// Validate an ELF file and map its PT_LOAD segments into p's address space,
// faulting their file data in on demand. Returns 0, or -ENOEXEC (logged) for
// anything we cannot run, -ENOMEM, or a read error; whatever was mapped is
// freed along with p's address space.
i32 elf_load(process_t* p, vnode_t* node, elf_info_t* out);
// Reserve disjoint image VMAs, preserving shared-page segment permissions.
i32 mm_map_image(process_t* p, vnode_t* node, const elf64_phdr_t* headers, u32 count);
