#pragma once
#include <types.h>
uint64_t get_usable_ram(void);
void pmm_init(void);
void *pmm_alloc(void);
void pmm_free(uint64_t addr);
void vmm_map(uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap(uint64_t virt);
void heap_init(void);
void *kmalloc(uint64_t size);
void kfree(void *addr);
extern volatile struct limine_memmap_request memmap_request;