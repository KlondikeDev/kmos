// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <types.h>
#include <limine.h>
#include <memory.h>
#include <memset.h>

#define PAGE_SIZE 4096

static uint8_t *bitmap;
static uint64_t total_pages;
static uint64_t hhdm_offset;

volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

uint64_t get_usable_ram(void) {
    uint64_t total = 0;
    struct limine_memmap_response *memmap = memmap_request.response;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            total += entry->length;
        }
    }
    return total;
}

void pmm_init(void) {
    hhdm_offset = hhdm_request.response->offset;
    uint64_t max_end = 0;
    struct limine_memmap_response *memmap = memmap_request.response;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        uint64_t end = entry->base + entry->length;
        if (end > max_end) {
            max_end = end;
        }
    }
    total_pages = max_end / PAGE_SIZE;
    uint64_t bitmap_size = (total_pages + 7) / 8;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        uint64_t aligned = (entry->base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= bitmap_size + (aligned - entry->base)) {
            bitmap = (uint8_t*)(aligned + hhdm_offset);
            break;
        }
    }
    if (!bitmap) {
        __asm__ volatile("int $0x82");
    }
    memset(bitmap, 0xFF, bitmap_size);
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        uint64_t start = entry->base / PAGE_SIZE;
        uint64_t count = entry->length / PAGE_SIZE;
        for (uint64_t j = start; j < start + count; j++) {
            if (entry->type == LIMINE_MEMMAP_USABLE) {
                bitmap[j / 8] &= ~(1 << (j % 8));
            }
        }
    }
    uint64_t phys = (uint64_t)bitmap - hhdm_offset;
    uint64_t bstart = phys / PAGE_SIZE;
    uint64_t bcount = (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for(uint64_t k = bstart; k < bstart + bcount; k++) {
        bitmap[k / 8] |= (1 << (k % 8));
    }
}

static uint64_t last_free_hint = 0;

void* pmm_alloc(void) {
    for (uint64_t i = last_free_hint; i < total_pages; i++) {
        if (!(bitmap[i / 8] & (1 << (i % 8)))) {
            bitmap[i / 8] |= (1 << (i % 8));
            last_free_hint = i + 1;
            return (void *)(i * PAGE_SIZE);
        }
    }
    return NULL;
}

void pmm_free(uint64_t addr) {
    if (addr % PAGE_SIZE != 0) {
        __asm__ volatile("int $0x82");
    }
    uint64_t i = addr / PAGE_SIZE;
    bitmap[i / 8] &= ~(1 << (i % 8));
}

void vmm_map(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t* pml4 = (uint64_t* )(cr3 + hhdm_offset);
    if (!(pml4[pml4_idx] & 1)) {
        void *page = pmm_alloc();
        memset((void *)((uint64_t)page + hhdm_offset), 0, PAGE_SIZE);
        pml4[pml4_idx] = (uint64_t)page | 3;
    }

    uint64_t *pdpt = (uint64_t *)((pml4[pml4_idx] & ~0xFFF) + hhdm_offset);
    if (!(pdpt[pdpt_idx] & 1)) {
        void *pd = pmm_alloc();
        memset((void *)((uint64_t)pd + hhdm_offset), 0, PAGE_SIZE);
        pdpt[pdpt_idx] = (uint64_t)pd | 3;
    }

    uint64_t *pd = (uint64_t *)((pdpt[pdpt_idx] & ~0xFFF) + hhdm_offset);
    if (!(pd[pd_idx] & 1)) {
        void *pt = pmm_alloc();
        memset((void *)((uint64_t)pt + hhdm_offset), 0, PAGE_SIZE);
        pd[pd_idx] = (uint64_t)pt | 3;
    }

    uint64_t *pt = (uint64_t *)((pd[pd_idx] & ~0xFFF) + hhdm_offset);
    pt[pt_idx] = phys | flags;
}

void vmm_unmap(uint64_t virt) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t* pml4 = (uint64_t* )(cr3 + hhdm_offset);
    if (!(pml4[pml4_idx] & 1)) {
        return;
    }

    uint64_t *pdpt = (uint64_t *)((pml4[pml4_idx] & ~0xFFF) + hhdm_offset);
    if (!(pdpt[pdpt_idx] & 1)) {
        return;
    }

    uint64_t *pd = (uint64_t *)((pdpt[pdpt_idx] & ~0xFFF) + hhdm_offset);
    if (!(pd[pd_idx] & 1)) {
        return;
    }

    uint64_t *pt = (uint64_t *)((pd[pd_idx] & ~0xFFF) + hhdm_offset);
    pt[pt_idx] = 0;
    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}