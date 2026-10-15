// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once
#include <types.h>
uint64_t get_usable_ram(void);
void pmm_init(void);
void *pmm_alloc(void);
void pmm_free(uint64_t addr);
void vmm_map(uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap(uint64_t virt);

// Address spaces, identified by the physical address of their PML4.
void vmm_init(void);                     // call once, right after heap_init
uint64_t vmm_hhdm(void);                 // phys + this = kernel-visible pointer
uint64_t vmm_kernel_space(void);
uint64_t vmm_current_space(void);
uint64_t vmm_create_space(void);         // empty lower half, shared kernel half
void vmm_destroy_space(uint64_t space);  // frees all user frames + tables
void vmm_switch(uint64_t space);
void vmm_map_in(uint64_t space, uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap_in(uint64_t space, uint64_t virt);
uint64_t vmm_get_pte(uint64_t space, uint64_t virt);  // 0 if not mapped
void heap_init(void);
void *kmalloc(uint64_t size);
void kfree(void *addr);
extern volatile struct limine_memmap_request memmap_request;

// Niche. Well, anyway, this file needs cleaning. TODO.