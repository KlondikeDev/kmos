// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <types.h>
#include <limine.h>
#include <memory.h>
#include <memset.h>

#define PAGE_SIZE 4096
#define PTE_USER_BIT 0x4

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

uint64_t pmm_total_pages(void) {
    return total_pages;
}

uint64_t pmm_count_free(void) {
    uint64_t n = 0;
    for (uint64_t i = 0; i < total_pages; i++) {
        if (!(bitmap[i / 8] & (1 << (i % 8)))) n++;
    }
    return n;
}

void pmm_free(uint64_t addr) {
    if (addr % PAGE_SIZE != 0) {
        __asm__ volatile("int $0x82");
    }
    uint64_t i = addr / PAGE_SIZE;
    bitmap[i / 8] &= ~(1 << (i % 8));
    if (i < last_free_hint) last_free_hint = i;
}

// ---------------------------------------------------------------------------
// Virtual memory
//
// Every address space is a PML4. The upper half (slots 256-511) is the
// kernel and is shared by all of them: each new PML4 gets copies of the
// kernel's upper-half entries, which point at the very same PDPTs. That only
// stays true if the kernel never adds a *new* upper-half PML4 entry later, so
// vmm_init() creates all 256 of them up front.
//
// The lower half (slots 0-255) is private to each process.
// ---------------------------------------------------------------------------

static uint64_t kernel_pml4;    // physical address of the boot address space

uint64_t vmm_hhdm(void) {
    return hhdm_offset;
}

static uint64_t* table_virt(uint64_t phys) {
    return (uint64_t*)(phys + hhdm_offset);
}

static uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & ~0xFFFULL;
}

// Return the next-level table under entry[idx], creating it if allowed.
static uint64_t* next_table(uint64_t* table, uint64_t idx, uint64_t user_bit, bool create) {
    if (!(table[idx] & 1)) {
        if (!create) return NULL;
        void* page = pmm_alloc();
        if (!page) __asm__ volatile("int $0x82");
        memset(table_virt((uint64_t)page), 0, PAGE_SIZE);
        table[idx] = (uint64_t)page | 3 | user_bit;
    } else {
        // Access rights are the AND of every level, so a page is only
        // reachable from ring 3 when every table above it is marked user too.
        table[idx] |= user_bit;
    }
    return table_virt(table[idx] & 0x000FFFFFFFFFF000ULL);
}

void vmm_init(void) {
    kernel_pml4 = read_cr3();
    uint64_t* pml4 = table_virt(kernel_pml4);
    for (int i = 256; i < 512; i++) {
        next_table(pml4, i, 0, true);
    }
}

uint64_t vmm_kernel_space(void) {
    return kernel_pml4;
}

uint64_t vmm_current_space(void) {
    return read_cr3();
}

void vmm_switch(uint64_t pml4) {
    if (read_cr3() != pml4) {
        __asm__ volatile("mov %0, %%cr3" :: "r"(pml4) : "memory");
    }
}

uint64_t vmm_create_space(void) {
    void* page = pmm_alloc();
    if (!page) return 0;
    uint64_t* pml4 = table_virt((uint64_t)page);
    uint64_t* kpml4 = table_virt(kernel_pml4);
    for (int i = 0; i < 256; i++) pml4[i] = 0;
    for (int i = 256; i < 512; i++) pml4[i] = kpml4[i];
    return (uint64_t)page;
}

// Free every user frame and every lower-half table, then the PML4 itself.
// Must not be called on the space currently loaded in CR3.
void vmm_destroy_space(uint64_t space) {
    uint64_t* pml4 = table_virt(space);
    for (int i = 0; i < 256; i++) {
        if (!(pml4[i] & 1)) continue;
        uint64_t* pdpt = table_virt(pml4[i] & 0x000FFFFFFFFFF000ULL);
        for (int j = 0; j < 512; j++) {
            if (!(pdpt[j] & 1)) continue;
            uint64_t* pd = table_virt(pdpt[j] & 0x000FFFFFFFFFF000ULL);
            for (int k = 0; k < 512; k++) {
                if (!(pd[k] & 1)) continue;
                uint64_t* pt = table_virt(pd[k] & 0x000FFFFFFFFFF000ULL);
                for (int l = 0; l < 512; l++) {
                    if (pt[l] & 1) pmm_free(pt[l] & 0x000FFFFFFFFFF000ULL);
                }
                pmm_free(pd[k] & 0x000FFFFFFFFFF000ULL);
            }
            pmm_free(pdpt[j] & 0x000FFFFFFFFFF000ULL);
        }
        pmm_free(pml4[i] & 0x000FFFFFFFFFF000ULL);
    }
    pmm_free(space);
}

// Copy every user page of src into dst (fork). Eager: each frame is
// duplicated now rather than shared copy-on-write. Returns false if memory
// runs out; dst then holds a partial copy for the caller to destroy.
bool vmm_clone_user(uint64_t src, uint64_t dst, uint32_t* pages_out) {
    uint64_t* pml4 = table_virt(src);
    uint32_t pages = 0;
    for (int i = 0; i < 256; i++) {
        if (!(pml4[i] & 1)) continue;
        uint64_t* pdpt = table_virt(pml4[i] & 0x000FFFFFFFFFF000ULL);
        for (int j = 0; j < 512; j++) {
            if (!(pdpt[j] & 1)) continue;
            uint64_t* pd = table_virt(pdpt[j] & 0x000FFFFFFFFFF000ULL);
            for (int k = 0; k < 512; k++) {
                if (!(pd[k] & 1)) continue;
                uint64_t* pt = table_virt(pd[k] & 0x000FFFFFFFFFF000ULL);
                for (int l = 0; l < 512; l++) {
                    uint64_t pte = pt[l];
                    if (!(pte & 1)) continue;
                    void* frame = pmm_alloc();
                    if (!frame) return false;
                    uint64_t from = pte & 0x000FFFFFFFFFF000ULL;
                    uint64_t* a = table_virt(from);
                    uint64_t* b = table_virt((uint64_t)frame);
                    for (int w = 0; w < PAGE_SIZE / 8; w++) b[w] = a[w];
                    uint64_t virt = ((uint64_t)i << 39) | ((uint64_t)j << 30) |
                                    ((uint64_t)k << 21) | ((uint64_t)l << 12);
                    vmm_map_in(dst, virt, (uint64_t)frame, pte & ~0x000FFFFFFFFFF000ULL);
                    pages++;
                }
            }
        }
    }
    *pages_out = pages;
    return true;
}

void vmm_map_in(uint64_t space, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t user_bit = flags & PTE_USER_BIT;
    uint64_t* pml4 = table_virt(space);
    uint64_t* pdpt = next_table(pml4, (virt >> 39) & 0x1FF, user_bit, true);
    uint64_t* pd   = next_table(pdpt, (virt >> 30) & 0x1FF, user_bit, true);
    uint64_t* pt   = next_table(pd,   (virt >> 21) & 0x1FF, user_bit, true);
    pt[(virt >> 12) & 0x1FF] = phys | flags;
    if (space == read_cr3()) {
        __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
    }
}

void vmm_unmap_in(uint64_t space, uint64_t virt) {
    uint64_t* pml4 = table_virt(space);
    uint64_t* pdpt = next_table(pml4, (virt >> 39) & 0x1FF, 0, false);
    if (!pdpt) return;
    uint64_t* pd = next_table(pdpt, (virt >> 30) & 0x1FF, 0, false);
    if (!pd) return;
    uint64_t* pt = next_table(pd, (virt >> 21) & 0x1FF, 0, false);
    if (!pt) return;
    pt[(virt >> 12) & 0x1FF] = 0;
    if (space == read_cr3()) {
        __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
    }
}

uint64_t vmm_get_pte(uint64_t space, uint64_t virt) {
    uint64_t* pml4 = table_virt(space);
    uint64_t* pdpt = next_table(pml4, (virt >> 39) & 0x1FF, 0, false);
    if (!pdpt) return 0;
    uint64_t* pd = next_table(pdpt, (virt >> 30) & 0x1FF, 0, false);
    if (!pd) return 0;
    uint64_t* pt = next_table(pd, (virt >> 21) & 0x1FF, 0, false);
    if (!pt) return 0;
    return pt[(virt >> 12) & 0x1FF];
}

void vmm_map(uint64_t virt, uint64_t phys, uint64_t flags) {
    vmm_map_in(read_cr3(), virt, phys, flags);
}

void vmm_unmap(uint64_t virt) {
    vmm_unmap_in(read_cr3(), virt);
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

void heap_stats(uint64_t* used, uint64_t* free_bytes) {
    *used = *free_bytes = 0;
    for (struct block_header* b = genesis_block_ptr; b; b = b->next_block) {
        if (b->free) *free_bytes += b->size;
        else *used += b->size;
    }
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