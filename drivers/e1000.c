// This file has been generated in whole or in part by a Large Language Model
// Intel 82540EM legacy descriptors. All packet work runs in the polling caller.
#include <e1000.h>
#include <pci.h>
#include <memory.h>
#include <ktime.h>
#include <io.h>

#define RING_COUNT 32
#define FRAME_MAX 1514
#define MMIO_BASE 0xffffc40000000000ULL
#define MMIO_SIZE 0x20000u
#define RESET_NS 100000000ULL

typedef struct {
    u64 address;
    u16 length, checksum;
    u8 status, errors;
    u16 special;
} rx_desc_t;
typedef struct {
    u64 address;
    u16 length;
    u8 checksum_offset, command, status, checksum_start;
    u16 special;
} tx_desc_t;
_Static_assert(sizeof(rx_desc_t) == 16, "e1000 RX descriptor size");
_Static_assert(sizeof(tx_desc_t) == 16, "e1000 TX descriptor size");

static struct {
    volatile u8* regs;
    volatile rx_desc_t* rx;
    volatile tx_desc_t* tx;
    u8* rx_buffers[RING_COUNT];
    u8* tx_buffers[RING_COUNT];
    u32 rx_next, tx_next;
    u8 mac[6];
    bool attempted, online, dropping;
} nic;

static u32 reg_read(u32 offset) { return *(volatile u32*)(nic.regs + offset); }
static void reg_write(u32 offset, u32 value) { *(volatile u32*)(nic.regs + offset) = value; }
static void fence(void) { __asm__ volatile("mfence" ::: "memory"); }
static void zero(void* buffer, u32 length) {
    u8* p = buffer; while (length--) *p++ = 0;
}
static void copy(void* destination, const void* source, u32 length) {
    u8* d = destination; const u8* s = source; while (length--) *d++ = *s++;
}
// The adjacent PCI status word contains write-one-to-clear bits. Touch command
// with a word write so enabling DMA cannot acknowledge unrelated PCI errors.
static void command_write(pci_device_t* dev, u16 command) {
    outl(PCI_CONFIG_ADDRESS, 0x80000004u | ((u32)dev->bus << 16) |
         ((u32)dev->device << 11) | ((u32)dev->function << 8));
    outw(PCI_CONFIG_DATA, command);
}
static bool wait_reset(void) {
    u64 start = time_counter_ns();
    do {
        u32 value = reg_read(0);
        if (value == ~0u) return false;
        if (!(value & (1u << 26))) return true;
        __asm__ volatile("pause" ::: "memory");
    } while (time_counter_ns() - start < RESET_NS);
    return false;
}
static void* dma_page(u64* physical) {
    *physical = (u64)pmm_alloc();
    if (!*physical) return NULL;
    void* p = (void*)(*physical + vmm_hhdm());
    zero(p, 4096);
    return p;
}
static void ring_address(u32 base, u64 physical) {
    reg_write(base, (u32)physical); reg_write(base + 4, physical >> 32);
    reg_write(base + 8, RING_COUNT * 16); reg_write(base + 16, 0);
}

bool e1000_init(void) {
    u64 flags = irq_save();
    if (nic.attempted) { irq_restore(flags); return nic.online; }
    nic.attempted = true;
    pci_device_t dev = pci_scan_for_device(PCI_VENDOR_INTEL, PCI_DEVICE_E1000);
    if (dev.vendor_id != PCI_VENDOR_INTEL || dev.device_id != PCI_DEVICE_E1000) goto fail;
    u32 bar = pci_read_config(dev.bus, dev.device, dev.function, 0x10);
    // This part has a 32-bit, naturally aligned 128 KiB register BAR.
    if ((bar & 15) || !(bar & ~15u) || (bar & 0x1ffffu) ||
        (u64)(bar & ~15u) + MMIO_SIZE > 0x100000000ULL) goto fail;
    nic.regs = (volatile u8*)MMIO_BASE;
    for (u32 off = 0; off < MMIO_SIZE; off += 4096)
        if (!vmm_map(MMIO_BASE + off, (u64)bar + off, 0x1b)) goto fail;
    u16 command = (u16)pci_read_config(dev.bus, dev.device, dev.function, 4);
    command_write(&dev, (command | 0x402u) & ~4u);
    reg_write(0xd8, ~0u); reg_write(0x100, 0); reg_write(0x400, 0);
    reg_write(0, reg_read(0) | (1u << 26));
    if (!wait_reset()) goto fail;
    // The EEPROM loads RAL/RAH during reset; no EEPROM busy loop is required.
    u32 low = reg_read(0x5400), high = reg_read(0x5404);
    if (!(high & (1u << 31))) goto fail;
    for (u32 i = 0; i < 4; i++) nic.mac[i] = low >> (i * 8);
    nic.mac[4] = high; nic.mac[5] = high >> 8;
    u8 any = 0; for (u32 i = 0; i < 6; i++) any |= nic.mac[i];
    if (!any || (nic.mac[0] & 1)) goto fail;
    reg_write(0xd8, ~0u); (void)reg_read(0xc0);
    u64 rx_phys, tx_phys;
    nic.rx = dma_page(&rx_phys); nic.tx = dma_page(&tx_phys);
    if (!nic.rx || !nic.tx) goto fail;
    for (u32 i = 0; i < RING_COUNT; i++) {
        u64 rp, tp;
        nic.rx_buffers[i] = dma_page(&rp); nic.tx_buffers[i] = dma_page(&tp);
        if (!nic.rx_buffers[i] || !nic.tx_buffers[i]) goto fail;
        nic.rx[i].address = rp; nic.tx[i].address = tp; nic.tx[i].status = 1;
    }
    for (u32 i = 0; i < 128; i++) reg_write(0x5200 + 4 * i, 0);
    ring_address(0x2800, rx_phys); reg_write(0x2818, RING_COUNT - 1);
    ring_address(0x3800, tx_phys); reg_write(0x3818, 0);
    reg_write(0x410, 10u | (8u << 10) | (6u << 20));
    // SLU, hardware auto-negotiation; receive strips CRC, accepts broadcast,
    // and uses 2048-byte buffers. TX inserts CRC and pads short frames.
    reg_write(0, (reg_read(0) | (1u << 6)) & ~((1u << 11) | (1u << 12)));
    fence(); command_write(&dev, command | 0x406u);
    reg_write(0x400, (1u << 1) | (1u << 3) | (15u << 4) | (64u << 12));
    reg_write(0x100, (1u << 1) | (1u << 15) | (1u << 26));
    nic.online = true;
    irq_restore(flags); return true;
fail:
    // Pages stay permanently reserved, including on partial initialization.
    // No caller memory is ever exposed to DMA; initialization is never retried.
    irq_restore(flags); return false;
}
bool e1000_link(void) { return nic.online && (reg_read(8) & 2) != 0 && reg_read(8) != ~0u; }
void e1000_mac(u8 out[6]) { if (out) copy(out, nic.mac, 6); }
bool e1000_send(const void* frame, u32 length) {
    if (!nic.online || !frame || length < 14 || length > FRAME_MAX) return false;
    u64 flags = irq_save();
    u32 index = nic.tx_next, next = (index + 1) % RING_COUNT;
    volatile tx_desc_t* d = &nic.tx[index];
    // Keep one slot unused: equal hardware head and tail means an empty ring.
    if (!(d->status & 1) || next == reg_read(0x3810)) {
        irq_restore(flags); return false;
    }
    fence(); copy(nic.tx_buffers[index], frame, length);
    d->length = length; d->checksum_offset = 0; d->checksum_start = 0;
    d->special = 0; d->command = 0xb; d->status = 0;
    fence(); nic.tx_next = next; reg_write(0x3818, next);
    irq_restore(flags); return true;
}
u32 e1000_receive(void* frame, u32 capacity) {
    if (!nic.online || !frame) return 0;
    u64 flags = irq_save();
    for (u32 count = 0; count < RING_COUNT; count++) {
        u32 index = nic.rx_next;
        volatile rx_desc_t* d = &nic.rx[index];
        u8 status = d->status;
        if (!(status & 1)) break;
        fence();
        u32 length = d->length;
        bool end = (status & 2) != 0;
        bool valid = end && !nic.dropping && !d->errors &&
                     length >= 14 && length <= FRAME_MAX && length <= capacity;
        if (valid) copy(frame, nic.rx_buffers[index], length);
        nic.dropping = !end;
        d->status = 0; d->errors = 0; d->length = 0;
        fence(); nic.rx_next = (index + 1) % RING_COUNT; reg_write(0x2818, index);
        if (valid) { irq_restore(flags); return length; }
    }
    irq_restore(flags); return 0;
}
