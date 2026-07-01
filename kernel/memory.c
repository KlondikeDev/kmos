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
    // Keep page 0 reserved so pmm_alloc can never return an address
    // equal to NULL, which callers treat as allocation failure.
    bitmap[0] |= 1;
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
    if (i < last_free_hint) last_free_hint = i;
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
        if (!page) __asm__ volatile("int $0x82");
        memset((void *)((uint64_t)page + hhdm_offset), 0, PAGE_SIZE);
        pml4[pml4_idx] = (uint64_t)page | 3;
    }

    uint64_t *pdpt = (uint64_t *)((pml4[pml4_idx] & ~0xFFF) + hhdm_offset);
    if (!(pdpt[pdpt_idx] & 1)) {
        void *pd = pmm_alloc();
        if (!pd) __asm__ volatile("int $0x82");
        memset((void *)((uint64_t)pd + hhdm_offset), 0, PAGE_SIZE);
        pdpt[pdpt_idx] = (uint64_t)pd | 3;
    }

    uint64_t *pd = (uint64_t *)((pdpt[pdpt_idx] & ~0xFFF) + hhdm_offset);
    if (!(pd[pd_idx] & 1)) {
        void *pt = pmm_alloc();
        if (!pt) __asm__ volatile("int $0x82");
        memset((void *)((uint64_t)pt + hhdm_offset), 0, PAGE_SIZE);
        pd[pd_idx] = (uint64_t)pt | 3;
    }

    uint64_t *pt = (uint64_t *)((pd[pd_idx] & ~0xFFF) + hhdm_offset);
    pt[pt_idx] = phys | flags;
    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
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

struct block_header {
    uint64_t size;
    uint64_t free;
    struct block_header* next_block;
};

uint64_t heap_base_addr;
struct block_header* genesis_block_ptr;
static struct block_header* heap_tail;   // last block in the chain
static uint64_t heap_end;                // next unmapped virtual address

void heap_init(void) {
    heap_base_addr = 0xFFFF900000000000;
    void* phys = pmm_alloc();
    if (!phys) __asm__ volatile("int $0x82");
    vmm_map(heap_base_addr, (uint64_t)phys, 3);

    genesis_block_ptr = (struct block_header* )heap_base_addr;

    genesis_block_ptr->size = PAGE_SIZE - sizeof(struct block_header);
    genesis_block_ptr->free = 1;
    genesis_block_ptr->next_block = NULL;

    heap_tail = genesis_block_ptr;
    heap_end = heap_base_addr + PAGE_SIZE;
}

// Map more pages at the end of the heap and append them as one free block.
static struct block_header* heap_grow(uint64_t need) {
    uint64_t pages = (need + sizeof(struct block_header) + PAGE_SIZE - 1) / PAGE_SIZE;
    struct block_header* blk = (struct block_header*)heap_end;
    for (uint64_t i = 0; i < pages; i++) {
        void* phys = pmm_alloc();
        if (!phys) return NULL;   // kmalloc reports this as allocation failure
        vmm_map(heap_end, (uint64_t)phys, 3);
        heap_end += PAGE_SIZE;
    }
    blk->size = pages * PAGE_SIZE - sizeof(struct block_header);
    blk->free = 1;
    blk->next_block = NULL;
    heap_tail->next_block = blk;
    heap_tail = blk;
    return blk;
}

// Carve `size` bytes out of an already-suitable free block, splitting the
// remainder off as a new free block when there's room for one.
static void* heap_carve(struct block_header* block, uint64_t size) {
    block->free = 0;
    if (block->size >= size + sizeof(struct block_header) + 1) {
        struct block_header *split = (struct block_header *)((uint64_t)(block + 1) + size);
        split->size = block->size - size - sizeof(struct block_header);
        split->free = 1;
        split->next_block = block->next_block;
        block->next_block = split;
        block->size = size;
        if (split->next_block == NULL) heap_tail = split;
    }
    return (void *)(block + 1);
}

void* kmalloc(uint64_t size) {
    struct block_header *current = genesis_block_ptr;

    while(current != NULL) {
        if (current->free == 1 && current->size >= size) {
            return heap_carve(current, size);
        }
        current = current->next_block;
    }

    struct block_header *grown = heap_grow(size);
    if (grown == NULL) return NULL;
    return heap_carve(grown, size);
}

void kfree(void* addr) {
    if (addr == NULL) return;
    struct block_header *block = (struct block_header *)addr - 1;
    block->free = 1;
    while (block->next_block != NULL && block->next_block->free == 1) {
        if (block->next_block == heap_tail) heap_tail = block;
        block->size += sizeof(struct block_header) + block->next_block->size;
        block->next_block = block->next_block->next_block;
    }
}