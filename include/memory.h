#pragma once
#include <types.h>
uint64_t get_usable_ram(void);
void pmm_init(void);
void* pmm_alloc(void);
void pmm_free(uint64_t addr);
extern volatile struct limine_memmap_request memmap_request;