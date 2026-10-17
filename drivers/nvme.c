// This file has been generated in whole or in part by a Large Language Model
// Polled NVM command set driver. Register and command layouts follow NVM
// Express 1.4c (https://nvmexpress.org/wp-content/uploads/NVM-Express-1_4c-2021.06.28-Ratified.pdf).
#include <nvme.h>
#include <pci.h>
#include <memory.h>
#include <io.h>
#include <ktime.h>
#include <klog.h>
#include <memcpy.h>

#define MAX_CONTROLLERS 8
#define MAX_NAMESPACES 8
#define QUEUE_DEPTH 16
#define MMIO_BASE 0xffffc30000000000ULL
#define MMIO_STRIDE 0x100000ULL
#define COMMAND_NS 5000000000ULL

// Commands and completions contain only naturally aligned little-endian fields.
typedef struct {
    u32 cdw0, nsid;
    u64 reserved, metadata, prp1, prp2;
    u32 cdw10, cdw11, cdw12, cdw13, cdw14, cdw15;
} command_t;
typedef struct {
    u32 result, reserved;
    u16 sqhead, sqid, cid, status;
} completion_t;
_Static_assert(sizeof(command_t) == 64, "NVMe submission size");
_Static_assert(sizeof(completion_t) == 16, "NVMe completion size");

typedef struct {
    command_t* sq;
    volatile completion_t* cq;
    u64 sq_phys, cq_phys;
    u16 id, depth, tail, head, cid;
    u8 phase;
} queue_t;
typedef struct {
    volatile u8* regs;
    queue_t admin, io;
    u64 bounce_phys, list_phys;
    u8* bounce;
    u32* list;
    u32 stride;
    u64 ready_ns;
    bool online;
} controller_t;
typedef struct {
    controller_t* controller;
    u32 id, block_size;
    u64 sectors;
} namespace_t;

static controller_t controllers[MAX_CONTROLLERS];
static namespace_t namespaces[MAX_NAMESPACES];
static u32 namespace_count, controller_count;
static bool initialized;

static void zero(void* ptr, u32 size) {
    u8* p = ptr;
    while (size--) *p++ = 0;
}
static u32 load32(const void* p) { u32 v; memcpy(&v, p, 4); return v; }
static u64 load64(const void* p) { u64 v; memcpy(&v, p, 8); return v; }
static u32 reg_read(controller_t* c, u32 offset) {
    return *(volatile u32*)(c->regs + offset);
}
static void reg_write(controller_t* c, u32 offset, u32 value) {
    *(volatile u32*)(c->regs + offset) = value;
}
static void reg_write64(controller_t* c, u32 offset, u64 value) {
    *(volatile u64*)(c->regs + offset) = value;
}
static void fence(void) { __asm__ volatile("mfence" ::: "memory"); }
static void pause_cpu(void) { __asm__ volatile("pause" ::: "memory"); }

static bool wait_ready(controller_t* c, bool ready) {
    u64 start = time_counter_ns();
    do {
        u32 status = reg_read(c, 0x1c);
        if (status == 0xffffffffu) return false;
        if ((status & 1) == (u32)ready) return true;
        if (ready && (status & 2)) return false;
        pause_cpu();
    } while (time_counter_ns() - start < c->ready_ns);
    return false;
}

// Never return these pages to the allocator, even if RDY refuses to clear.
// A dead controller is never retried, and can only DMA to its reserved pages.
// Callers' buffers are never handed to the hardware. This bounded quarantine
// also covers a controller disappearing halfway through a command.
static void fail_controller(controller_t* c, const char* reason) {
    c->online = false;
    reg_write(c, 0x14, reg_read(c, 0x14) & ~1u);
    bool stopped = wait_ready(c, false);
    klog(LOG_ERROR, "nvme: %s; controller disabled (%s), DMA pages reserved\n",
         reason, stopped ? "quiesced" : "timeout");
}

static bool submit(controller_t* c, queue_t* q, command_t* cmd) {
    if (!c->online || (reg_read(c, 0x1c) & 3) != 1) {
        if (c->online) fail_controller(c, "controller not ready");
        return false;
    }
    u16 cid = ++q->cid;
    cmd->cdw0 = (cmd->cdw0 & 0xffffu) | ((u32)cid << 16);
    memcpy(&q->sq[q->tail], cmd, sizeof(*cmd));
    q->tail = (q->tail + 1) % q->depth;
    fence();
    reg_write(c, 0x1000 + (2u * q->id) * c->stride, q->tail);
    u64 start = time_counter_ns();
    for (;;) {
        volatile completion_t* entry = &q->cq[q->head];
        u16 status = entry->status;
        if ((status & 1) == q->phase) {
            fence();
            // One outstanding command means every completion must be ours.
            if (entry->cid != cid || entry->sqid != q->id ||
                entry->sqhead >= q->depth) {
                fail_controller(c, "invalid completion");
                return false;
            }
            q->head++;
            if (q->head == q->depth) { q->head = 0; q->phase ^= 1; }
            fence();
            reg_write(c, 0x1000 + (2u * q->id + 1) * c->stride, q->head);
            if ((status >> 1) & 0x7ff) {
                klog(LOG_WARN, "nvme: command 0x%x status 0x%x\n", cmd->cdw0 & 255, status);
                return false;
            }
            return true;
        }
        if ((reg_read(c, 0x1c) & 3) != 1 ||
            time_counter_ns() - start >= COMMAND_NS) {
            fail_controller(c, "command timeout or fatal status");
            return false;
        }
        pause_cpu();
    }
}

static void* dma_page(u64* physical) {
    *physical = (u64)pmm_alloc();
    if (!*physical) return NULL;
    void* mapped = (void*)(*physical + vmm_hhdm());
    zero(mapped, 4096);
    return mapped;
}
static bool allocate_queue(queue_t* q, u16 id, u16 depth) {
    q->id = id; q->depth = depth; q->phase = 1;
    q->sq = dma_page(&q->sq_phys);
    q->cq = dma_page(&q->cq_phys);
    return q->sq && q->cq;
}
static bool identify(controller_t* c, u32 id, u32 cns, u64 physical) {
    command_t cmd = { .cdw0 = 6, .nsid = id, .prp1 = physical, .cdw10 = cns };
    return submit(c, &c->admin, &cmd);
}
static bool add_namespace(controller_t* c, u32 id) {
    if (!id || id == 0xffffffffu || !identify(c, id, 0, c->bounce_phys)) return false;
    u8* data = c->bounce;
    u64 size = load64(data);
    u8 format = data[26] & 15;
    // Reject metadata (separate or extended), protection information, and
    // formats this driver cannot represent. Never silently discard metadata.
    u32 lbaf = load32(data + 128 + format * 4);
    u32 exponent = (lbaf >> 16) & 255;
    if (!size || format > data[25] || (lbaf & 0xffff) || (data[29] & 7) ||
        (data[26] & 0xe0) || (exponent != 9 && exponent != 12) ||
        size > (~0ULL >> (exponent - 9))) return false;
    namespace_t* ns = &namespaces[namespace_count++];
    ns->controller = c; ns->id = id; ns->block_size = 1u << exponent;
    ns->sectors = size << (exponent - 9);
    klog(LOG_OKAY, "nvme: namespace %u index %u, %llu sectors, %u-byte blocks\n",
         id, namespace_count - 1, ns->sectors, ns->block_size);
    return true;
}

static bool start_controller(controller_t* c, u64 bar, u32 slot) {
    if (!bar || (bar & 4095) || bar >= (1ULL << 52) ||
        bar > (1ULL << 52) - MMIO_STRIDE) return false;
    c->regs = (volatile u8*)(MMIO_BASE + slot * MMIO_STRIDE);
    if (!vmm_map((u64)c->regs, bar, 0x1b)) return false;
    u64 cap = *(volatile u64*)c->regs;
    // 4 KiB host pages, NVM command set, and at least two queue entries.
    if (cap == ~0ULL || ((cap >> 48) & 15) || !(cap & (1ULL << 37)) ||
        !(cap & 0xffff)) return false;
    c->stride = 4u << ((cap >> 32) & 15);
    u64 length = (0x1000 + 3 * c->stride + 4 + 4095) & ~4095ULL;
    if (length > MMIO_STRIDE) return false;
    for (u64 off = 4096; off < length; off += 4096)
        if (!vmm_map((u64)c->regs + off, bar + off, 0x1b)) return false;
    c->ready_ns = (((cap >> 24) & 255) + 1) * 500000000ULL;
    reg_write(c, 0x0c, 0xffffffffu); // mask legacy/MSI interrupts; queues poll
    reg_write(c, 0x14, reg_read(c, 0x14) & ~1u);
    if (!wait_ready(c, false)) return false;
    u16 depth = (cap & 0xffff) >= QUEUE_DEPTH - 1 ? QUEUE_DEPTH : (cap & 0xffff) + 1;
    if (!allocate_queue(&c->admin, 0, depth) || !allocate_queue(&c->io, 1, depth)) return false;
    c->bounce = dma_page(&c->bounce_phys);
    c->list = dma_page(&c->list_phys);
    if (!c->bounce || !c->list) return false;
    reg_write(c, 0x24, (depth - 1) | ((u32)(depth - 1) << 16));
    reg_write64(c, 0x28, c->admin.sq_phys);
    reg_write64(c, 0x30, c->admin.cq_phys);
    fence();
    reg_write(c, 0x14, 1 | (6u << 16) | (4u << 20));
    if (!wait_ready(c, true)) { fail_controller(c, "enable timeout"); return false; }
    c->online = true;
    if (!identify(c, 0, 1, c->bounce_phys)) goto failed;
    u32 nn = load32(c->bounce + 516);
    // Verify the advertised SQ/CQ entry size ranges include 64/16 bytes.
    if ((c->bounce[512] & 15) > 6 || (c->bounce[512] >> 4) < 6 ||
        (c->bounce[513] & 15) > 4 || (c->bounce[513] >> 4) < 4) goto failed;
    command_t cmd = { .cdw0 = 9, .cdw10 = 7, .cdw11 = 0 };
    if (!submit(c, &c->admin, &cmd)) goto failed; // request one queue pair
    cmd = (command_t){ .cdw0 = 5, .prp1 = c->io.cq_phys,
        .cdw10 = 1 | ((u32)(depth - 1) << 16), .cdw11 = 1 };
    if (!submit(c, &c->admin, &cmd)) goto failed;
    cmd = (command_t){ .cdw0 = 1, .prp1 = c->io.sq_phys,
        .cdw10 = 1 | ((u32)(depth - 1) << 16), .cdw11 = 1 | (1u << 16) };
    if (!submit(c, &c->admin, &cmd)) goto failed;
    // NVMe 1.1 introduced the active list. Older controllers get a bounded
    // sequential scan. Only the first 1024 active namespaces are considered.
    u32 version = reg_read(c, 8);
    if (version >= 0x10100) {
        if (!identify(c, 0, 2, c->list_phys)) goto failed;
        u32 previous = 0;
        for (u32 i = 0; i < 1024 && namespace_count < MAX_NAMESPACES; i++) {
            u32 id = c->list[i];
            if (!id) break;
            if (id <= previous || id == 0xffffffffu) break;
            previous = id;
            add_namespace(c, id);
            if (!c->online) return false;
        }
    } else {
        if (nn > 1024) nn = 1024;
        for (u32 id = 1; id <= nn && namespace_count < MAX_NAMESPACES; id++) {
            add_namespace(c, id);
            if (!c->online) return false;
        }
    }
    return true;
failed:
    if (c->online) fail_controller(c, "initialization failed");
    return false;
}

// The status half of PCI command/status is write-one-to-clear. Use a word
// write so enabling DMA cannot accidentally clear pending PCI errors.
static void enable_pci(u8 bus, u8 dev, u8 function) {
    u32 address = (1u << 31) | ((u32)bus << 16) | ((u32)dev << 11) |
                  ((u32)function << 8) | 4;
    outl(PCI_CONFIG_ADDRESS, address);
    u16 command = inw(PCI_CONFIG_DATA);
    outw(PCI_CONFIG_DATA, command | 6 | (1u << 10));
}
void nvme_init(void) {
    u64 flags = irq_save();
    if (initialized) { irq_restore(flags); return; }
    initialized = true;
    for (u32 bus = 0; bus < 256 && controller_count < MAX_CONTROLLERS && namespace_count < MAX_NAMESPACES; bus++)
        for (u32 dev = 0; dev < 32 && controller_count < MAX_CONTROLLERS && namespace_count < MAX_NAMESPACES; dev++) {
            if ((pci_read_config(bus, dev, 0, 0) & 0xffff) == 0xffff) continue;
            u32 functions = (pci_read_config(bus, dev, 0, 0x0c) & 0x800000) ? 8 : 1;
            for (u32 fn = 0; fn < functions && controller_count < MAX_CONTROLLERS && namespace_count < MAX_NAMESPACES; fn++) {
                if ((pci_read_config(bus, dev, fn, 0) & 0xffff) == 0xffff ||
                    (pci_read_config(bus, dev, fn, 8) >> 8) != 0x010802) continue;
                u32 low = pci_read_config(bus, dev, fn, 0x10);
                if (low & 1) continue;
                u32 type = (low >> 1) & 3;
                if (type != 0 && type != 2) continue;
                u64 bar = low & ~15u;
                if (type == 2) bar |= (u64)pci_read_config(bus, dev, fn, 0x14) << 32;
                enable_pci(bus, dev, fn);
                u32 slot = controller_count++;
                start_controller(&controllers[slot], bar, slot);
            }
        }
    irq_restore(flags);
}
u32 nvme_count(void) { return namespace_count; }
u64 nvme_sectors(u8 index) { return index < namespace_count ? namespaces[index].sectors : 0; }
u32 nvme_block_size(u8 index) { return index < namespace_count ? namespaces[index].block_size : 0; }

static bool native_transfer(namespace_t* ns, u64 lba, u32 blocks, bool write) {
    controller_t* c = ns->controller;
    command_t cmd = { .cdw0 = write ? 1 : 2, .nsid = ns->id,
        .prp1 = c->bounce_phys, .cdw10 = (u32)lba, .cdw11 = lba >> 32,
        .cdw12 = blocks - 1 };
    return submit(c, &c->io, &cmd);
}
static bool transfer(u8 index, u64 lba, u32 count, void* buffer, bool write) {
    if (index >= namespace_count) return false;
    namespace_t* ns = &namespaces[index];
    if (lba > ns->sectors || count > ns->sectors - lba || (!buffer && count)) return false;
    u64 flags = irq_save();
    controller_t* c = ns->controller;
    bool okay = c->online;
    u8* bytes = buffer;
    u32 unit = ns->block_size / 512;
    while (okay && count) {
        u32 offset = lba % unit;
        u32 chunk = unit == 1 ? (count > 8 ? 8 : count) : unit - offset;
        if (chunk > count) chunk = count;
        u32 blocks = unit == 1 ? chunk : 1;
        bool partial = offset || chunk < unit;
        if (!write || partial) okay = native_transfer(ns, lba / unit, blocks, false);
        if (!okay) break;
        if (write) {
            memcpy(c->bounce + offset * 512, bytes, chunk * 512);
            okay = native_transfer(ns, lba / unit, blocks, true);
        } else memcpy(bytes, c->bounce + offset * 512, chunk * 512);
        lba += chunk; count -= chunk; bytes += chunk * 512;
    }
    irq_restore(flags);
    return okay;
}
bool nvme_read(u8 index, u64 lba, u32 count, void* buffer) {
    return transfer(index, lba, count, buffer, false);
}
bool nvme_write(u8 index, u64 lba, u32 count, const void* buffer) {
    return transfer(index, lba, count, (void*)buffer, true);
}
bool nvme_flush(u8 index) {
    if (index >= namespace_count) return false;
    u64 flags = irq_save();
    namespace_t* ns = &namespaces[index];
    command_t cmd = { .nsid = ns->id };
    bool okay = submit(ns->controller, &ns->controller->io, &cmd);
    irq_restore(flags);
    return okay;
}
