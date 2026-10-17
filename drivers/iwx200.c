// This file has been generated in whole or in part by a Large Language Model
// Intel Wi-Fi 6 AX200 bring-up: PCI probe, CSR init, persistence clearing,
// MAC address, INIT firmware load and the alive handshake. RX/TX queues and
// the 802.11 state machine arrive later; this file only takes the card from
// cold to firmware-alive. Everything runs in the caller's context with IRQs
// excluded, like the e1000 driver: no blocking, no scheduler calls, every
// hardware wait bounded by the TSC clock.
#include <iwx200.h>
#include <wifi.h>
#include <wifi_wpa.h>
#include <wifi_crypto.h>
#include <pci.h>
#include <memory.h>
#include <ktime.h>
#include <io.h>
#include <file.h>
#include <klog.h>

#define MMIO_BASE 0xffffc42000000000ULL
#define MMIO_SIZE 0x20000u

// Gen2 context info prefix: version, control, receive/command ring
// addresses, then the firmware image tables. LMAC entries precede the
// CPU1/CPU2 separator, UMAC follows it, paging follows the paging
// separator. Offsets match the firmware's layout.
typedef struct {
    u16 mac_id, version, size, reserved_ver;
    u32 control_flags, reserved_ctrl;
    u64 reserved0;
    u64 rx_free, rx_used, rx_status;
    u64 cmd_addr;
    u16 cmd_size, reserved_cmd;
    u8 reserved_a[4];
    u64 umac_image[16];
    u64 lmac_image[16];
    u64 paging_image[32];
} ctxt_t;
_Static_assert(sizeof(ctxt_t) == 576, "AX200 gen2 context prefix size");

static struct {
    volatile u8* regs;
    u8 mac[6];
    bool attempted, online, alive;
    bool firmware_ok;
    iwx200_fw_t fw;
    u64 ctxt_phys;
    // Command transport: DMA rings plus host-side mirrors.
    volatile u8* tx_ring; // 256 TFDs of 256 bytes
    volatile u8* cmd_data; // 256 command buffers of 324 bytes
    u64 cmd_phys; // physical address of cmd_data
    u64 tx_phys; // physical address of tx_ring
    u64 rx_free_phys; // physical address of the RX free list
    u64 rx_status_phys; // physical address of the RX status word
    u64* rx_free; // 512 receive buffer addresses (host mirror kept too)
    u8* rx_buffers[IWX_RX_RING_COUNT];
    volatile u16* rx_status; // closed-buffer index written by firmware
    u32 tx_next, rx_next;
    u8 tx_index; // next command slot identifier
    // Last unsolicited notification, for init-complete and scan events.
    u8 notif_code, notif_group;
    u8 notif_payload[256];
    u32 notif_length;
    bool notif_pending;
} card;

static u32 reg_read(u32 offset) { return *(volatile u32*)(card.regs + offset); }
static bool transport_alloc(void);
static void reg_write(u32 offset, u32 value) { *(volatile u32*)(card.regs + offset) = value; }
static void reg_set(u32 offset, u32 bits) { reg_write(offset, reg_read(offset) | bits); }
static void fence(void) { __asm__ volatile("mfence" ::: "memory"); }
static void zero(void* buffer, u32 length) {
    u8* p = buffer; while (length--) *p++ = 0;
}
static void copy(void* destination, const void* source, u32 length) {
    u8* d = destination; const u8* s = source; while (length--) *d++ = *s++;
}
// The adjacent PCI status word holds write-one-to-clear bits, so enable DMA
// with a word write that cannot acknowledge unrelated PCI errors.
static void command_write(pci_device_t* dev, u16 command) {
    outl(PCI_CONFIG_ADDRESS, 0x80000004u | ((u32)dev->bus << 16) |
         ((u32)dev->device << 11) | ((u32)dev->function << 8));
    outw(PCI_CONFIG_DATA, command);
}
// True when the masked bits equal the wanted value before the deadline.
static bool poll(u32 offset, u32 mask, u32 want, u64 nanoseconds) {
    u64 start = time_counter_ns();
    for (;;) {
        u32 value = reg_read(offset);
        if (value == ~0u) return false;
        if ((value & mask) == want) return true;
        if (time_counter_ns() - start >= nanoseconds) return false;
        __asm__ volatile("pause" ::: "memory");
    }
}
static void* dma_page(u64* physical) {
    *physical = (u64)pmm_alloc();
    if (!*physical) return NULL;
    void* p = (void*)(*physical + vmm_hhdm());
    zero(p, 4096);
    return p;
}
// Contiguous DMA for firmware sections: gather single pages until a
// physically contiguous run of the needed length appears. Bounded: at
// most 80 pages are held while searching. The run stays allocated; every
// other page is released. NULL when memory is fragmented or short.
static void* dma_contig(u64* physical, u32 length) {
    u32 want = (length + 4095) / 4096;
    if (!want || want > 32) return NULL;
    u64 pages[80 + 32];
    u32 held = 0;
    while (held < sizeof(pages) / sizeof(pages[0]) - 32) {
        pages[held] = (u64)pmm_alloc();
        if (!pages[held]) break;
        held++;
        if (held < want) continue;
        for (u32 start = 0; start + want <= held; start++) {
            bool contiguous = true;
            for (u32 i = 1; i < want; i++)
                if (pages[start + i] != pages[start] + (u64)i * 4096) { contiguous = false; break; }
            if (!contiguous) continue;
            *physical = pages[start];
            void* p = (void*)(*physical + vmm_hhdm());
            zero(p, want * 4096);
            for (u32 i = 0; i < held; i++) {
                bool in_run = i >= start && i < start + want;
                if (!in_run) pmm_free(pages[i]);
            }
            return p;
        }
    }
    for (u32 i = 0; i < held; i++) pmm_free(pages[i]);
    return NULL;
}
// Indirect peripheral access through the HBUS window. The window address
// registers select a dword; data moves through the read/write registers.
// Every access is bounded by construction (no wait loops).
static void prph_write(u32 address, u32 value) {
    reg_write(IWX_HBUS_PRPH_WADDR, address);
    fence();
    reg_write(IWX_HBUS_PRPH_WDATA, value);
    fence();
}
static u32 prph_read(u32 address) {
    reg_write(IWX_HBUS_PRPH_RADDR, address);
    fence();
    return reg_read(IWX_HBUS_PRPH_RDATA);
}
// MAC access arbitration: request the window, wait for the grant unless the
// card reports it is falling asleep.
static bool nic_lock(void) {
    reg_set(IWX_CSR_GP_CNTRL, IWX_GP_MAC_ACCESS_REQ);
    __asm__ volatile("pause" ::: "memory");
    __asm__ volatile("pause" ::: "memory");
    return poll(IWX_CSR_GP_CNTRL, IWX_GP_MAC_CLOCK_READY | IWX_GP_GOING_TO_SLEEP,
                IWX_GP_MAC_ACCESS_EN, IWX_ACCESS_NS);
}
// Tell the card the host OS is up, then prepare it for firmware loading.
static bool prepare_card(void) {
    reg_set(IWX_CSR_HW_IF_CONFIG, IWX_HW_IF_NIC_READY);
    if (poll(IWX_CSR_HW_IF_CONFIG, IWX_HW_IF_NIC_READY, IWX_HW_IF_NIC_READY, IWX_RESET_NS)) {
        reg_set(IWX_CSR_MBOX_SET, IWX_MBOX_OS_ALIVE);
        return true;
    }
    reg_set(IWX_CSR_DBG_LINK_PMGMT, IWX_RESET_LINK_PMGMT_DISABLED);
    u64 start = time_counter_ns();
    for (u32 attempt = 0; attempt < 10; attempt++) {
        reg_set(IWX_CSR_HW_IF_CONFIG, IWX_HW_IF_PREPARE);
        u64 inner = time_counter_ns();
        for (;;) {
            reg_set(IWX_CSR_HW_IF_CONFIG, IWX_HW_IF_NIC_READY);
            if (poll(IWX_CSR_HW_IF_CONFIG, IWX_HW_IF_NIC_READY, IWX_HW_IF_NIC_READY, IWX_RESET_NS)) {
                reg_set(IWX_CSR_MBOX_SET, IWX_MBOX_OS_ALIVE);
                return true;
            }
            if (time_counter_ns() - inner >= IWX_PREPARE_NS) break;
            __asm__ volatile("pause" ::: "memory");
        }
        if (time_counter_ns() - start >= 10 * (IWX_PREPARE_NS + 25000000ULL)) break;
    }
    return false;
}
// Clock and power init for the discrete 22000 path.
static bool apm_init(void) {
    reg_set(IWX_CSR_GIO_CHICKEN, 0x00800000u);
    reg_set(IWX_CSR_DBG_HPET_MEM, 0xFFFF0000u);
    reg_set(IWX_CSR_HW_IF_CONFIG, IWX_HW_IF_HAP_WAKE_L1A);
    reg_set(IWX_CSR_GIO_CHICKEN - 0xc4, 0x00000002u); // GIO_REG at 0x03c
    reg_set(IWX_CSR_GP_CNTRL, IWX_GP_INIT_DONE);
    return poll(IWX_CSR_GP_CNTRL, IWX_GP_MAC_CLOCK_READY, IWX_GP_MAC_CLOCK_READY, IWX_CLOCK_NS);
}
// A previous boot may have left its persistence bit set; clear it through
// the write-protect register, refusing when the register reports locked.
static bool clear_persistence(void) {
    if (!nic_lock()) return false;
    u32 debug = prph_read(IWX_HPM_DEBUG);
    bool ok = true;
    if (debug != 0xa5a5a5a0u && (debug & IWX_HPM_PERSISTENCE)) {
        if (prph_read(IWX_PREG_WPROT_22000) & IWX_WPROT_LOCKED) ok = false;
        else prph_write(IWX_HPM_DEBUG, debug & ~IWX_HPM_PERSISTENCE);
    }
    reg_write(IWX_CSR_GP_CNTRL, reg_read(IWX_CSR_GP_CNTRL) & ~IWX_GP_MAC_ACCESS_REQ);
    return ok;
}
// The MAC address ships in strap registers off the MAC CSR base, most
// significant byte first. Reject the unprogrammed and multicast values.
static bool read_mac(void) {
    if (!nic_lock()) return false;
    u32 low = reg_read(IWX_CSR_MAC_CSR_BASE + IWX_MAC_STRAP0);
    u32 high = reg_read(IWX_CSR_MAC_CSR_BASE + IWX_MAC_STRAP1);
    reg_write(IWX_CSR_GP_CNTRL, reg_read(IWX_CSR_GP_CNTRL) & ~IWX_GP_MAC_ACCESS_REQ);
    if (low == ~0u || high == ~0u) return false;
    card.mac[0] = (u8)(low >> 24); card.mac[1] = (u8)(low >> 16);
    card.mac[2] = (u8)(low >> 8); card.mac[3] = (u8)low;
    card.mac[4] = (u8)(high >> 8); card.mac[5] = (u8)high;
    u8 any = 0; for (u32 i = 0; i < 6; i++) any |= card.mac[i];
    if (!any || (card.mac[0] & 1)) return false;
    return true;
}
// INIT firmware image: parse the TLV container and record the INIT code
// sections plus the API version. RT sections are for later (post-alive).
bool iwx200_parse_fw(const void* image, u64 size, iwx200_fw_t* out) {
    if (!image || !out || size < 104 + 8) return false;
    const u8* p = image;
    zero(out, sizeof(*out));
    u32 zero_word = (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
    u32 magic = (u32)p[4] | ((u32)p[5] << 8) | ((u32)p[6] << 16) | ((u32)p[7] << 24);
    if (zero_word != 0 || magic != IWX_TLV_MAGIC) return false;
    u32 version = (u32)p[72] | ((u32)p[73] << 8) | ((u32)p[74] << 16) | ((u32)p[75] << 24);
    out->api = version & 0xff;
    // Header: zero(4) magic(4) name(64) ver(4) build(4) ignore(8) = 88 bytes.
    u64 offset = 88;
    while (offset + 8 <= size) {
        u32 type = (u32)p[offset] | ((u32)p[offset+1] << 8) | ((u32)p[offset+2] << 16) | ((u32)p[offset+3] << 24);
        u32 length = (u32)p[offset+4] | ((u32)p[offset+5] << 8) | ((u32)p[offset+6] << 16) | ((u32)p[offset+7] << 24);
        offset += 8;
        if (length > size - offset) return false;
        if ((type == IWX_TLV_SEC_INIT || type == IWX_TLV_SEC_RT) && length >= 4) {
            // Each section TLV is one entry: the first dword is the
            // device offset, the rest is code. Separator offsets mark
            // the LMAC/UMAC/paging boundaries between entries.
            iwx200_sect_t* table = type == IWX_TLV_SEC_INIT ? out->init : out->regular;
            u32* count = type == IWX_TLV_SEC_INIT ? &out->init_count : &out->regular_count;
            if (*count >= IWX_UCODE_SECT_MAX) return false;
            u32 device_offset = (u32)p[offset] | ((u32)p[offset+1] << 8) |
                                ((u32)p[offset+2] << 16) | ((u32)p[offset+3] << 24);
            table[*count].address = (u64)(p + offset + 4);
            table[*count].device_offset = device_offset;
            table[*count].length = length - 4;
            (*count)++;
        } else if (type == IWX_TLV_FW_VERSION && length == 12) {
            u32 major = (u32)p[offset] | ((u32)p[offset+1] << 8) |
                        ((u32)p[offset+2] << 16) | ((u32)p[offset+3] << 24);
            u32 minor = (u32)p[offset+4] | ((u32)p[offset+5] << 8) |
                        ((u32)p[offset+6] << 16) | ((u32)p[offset+7] << 24);
            u32 api = (u32)p[offset+8] | ((u32)p[offset+9] << 8) |
                      ((u32)p[offset+10] << 16) | ((u32)p[offset+11] << 24);
            u32 i = 0;
            // "major.minor.api" into the fixed buffer, bounded.
            u32 values[3] = { major, minor, api };
            for (u32 v = 0; v < 3; v++) {
                char digits[12]; u32 digits_length = 0;
                u32 value = values[v];
                if (!value) digits[digits_length++] = '0';
                else { char reversed[12]; u32 count = 0;
                    while (value && count < sizeof(reversed)) { reversed[count++] = (char)('0' + value % 10); value /= 10; }
                    while (count) digits[digits_length++] = reversed[--count]; }
                for (u32 d = 0; d < digits_length && i + 1 < sizeof(out->version); d++) out->version[i++] = digits[d];
                if (v < 2 && i + 1 < sizeof(out->version)) out->version[i++] = '.';
            }
            out->version[i] = 0;
        }
        offset += (length + 3) & ~3u;
    }
    return out->init_count > 0 || out->regular_count > 0;
}
// Load a staged REGULAR image: copy sections into DMA pages, describe them
// in context info, point the card at it and kick the CPU. The image must
// have been parsed by iwx200_parse_fw; pages stay permanently reserved.
static bool load_firmware(void) {
    if (!card.firmware_ok || !card.fw.regular_count) return false;
    if (!transport_alloc()) return false;
    u64 ctxt_phys;
    ctxt_t* ctxt = dma_page(&ctxt_phys);
    if (!ctxt) return false;
    card.ctxt_phys = ctxt_phys;
    ctxt->mac_id = (u16)(reg_read(IWX_CSR_HW_REV) & 0xffff);
    ctxt->version = 0;
    ctxt->size = sizeof(*ctxt) / 4;
    ctxt->control_flags = IWX_CTXT_TFD_LONG | (9u << IWX_CTXT_RB_CB_POS) |
                          (IWX_CTXT_RB_4K << IWX_CTXT_RB_SIZE_POS);
    // RX free list, used list (unused, firmware-owned) and status pointer
    // for the firmware's receive DMA; command queue address and size.
    ctxt->rx_free = card.rx_free_phys;
    ctxt->rx_used = card.rx_free_phys; // firmware ignores the used list
    ctxt->rx_status = card.rx_status_phys;
    ctxt->cmd_addr = card.tx_phys;
    ctxt->cmd_size = 5; // CB_SIZE encoding for 256 entries
    ctxt->reserved_cmd = 0;
    u32 lmac = 0, umac = 0, paging = 0, group = 0;
    for (u32 i = 0; i < card.fw.regular_count; i++) {
        u32 device_offset = card.fw.regular[i].device_offset;
        // Separator entries advance the group without consuming a page.
        if (device_offset == IWX_SEP_CPU12) { group = 1; continue; }
        if (device_offset == IWX_SEP_PAGING) { group = 2; continue; }
        u64* table = group == 0 ? ctxt->lmac_image : group == 1 ? ctxt->umac_image : ctxt->paging_image;
        u32* count = group == 0 ? &lmac : group == 1 ? &umac : &paging;
        u32 limit = group == 2 ? 32 : 16;
        if (*count >= limit) return false;
        u64 section_phys;
        u8* section = dma_contig(&section_phys, card.fw.regular[i].length);
        if (!section) return false;
        copy(section, (const void*)card.fw.regular[i].address, card.fw.regular[i].length);
        table[(*count)++] = section_phys;
    }
    if (!lmac || !umac) return false;
    fence();
    reg_write(IWX_CSR_CTXT_INFO_BA, (u32)ctxt_phys);
    reg_write(IWX_CSR_CTXT_INFO_BA + 4, (u32)(ctxt_phys >> 32));
    if (!nic_lock()) return false;
    // 250 usec latency tolerance while the firmware self-loads.
    reg_write(IWX_CSR_LTR_LONG_VAL, 0x80FA80FAu);
    prph_write(IWX_UREG_CPU_INIT_RUN, 1);
    reg_write(IWX_CSR_GP_CNTRL, reg_read(IWX_CSR_GP_CNTRL) & ~IWX_GP_MAC_ACCESS_REQ);
    fence();
    return true;
}
// The firmware announces itself through the UC_GP1 register once the INIT
// image runs. Poll bounded; the RX-path notification arrives later.
static bool wait_alive(void) {
    u64 start = time_counter_ns();
    for (;;) {
        u32 gp1 = reg_read(IWX_CSR_UCODE_DRV_GP1);
        if (gp1 == ~0u) return false;
        if (gp1 & IWX_GP1_INIT_COMPLETE) { card.alive = true; return true; }
        if (time_counter_ns() - start >= IWX_ALIVE_NS) return false;
        __asm__ volatile("pause" ::: "memory");
    }
}
bool iwx200_init(void) {
    u64 flags = irq_save();
    if (card.attempted) { irq_restore(flags); return card.online; }
    card.attempted = true;
    pci_device_t dev = pci_scan_for_device(PCI_VENDOR_INTEL, IWX_PCI_AX200);
    if (dev.vendor_id != PCI_VENDOR_INTEL || dev.device_id != IWX_PCI_AX200) goto fail;
    u32 bar = pci_read_config(dev.bus, dev.device, dev.function, 0x10);
    // A 32-bit, naturally aligned 128 KiB register BAR below 4 GiB.
    if ((bar & 15) || !(bar & ~15u) || (bar & 0x1ffffu) ||
        (u64)(bar & ~15u) + MMIO_SIZE > 0x100000000ULL) goto fail;
    card.regs = (volatile u8*)MMIO_BASE;
    for (u32 off = 0; off < MMIO_SIZE; off += 4096)
        if (!vmm_map(MMIO_BASE + off, (u64)bar + off, 0x1b)) goto fail;
    u16 command = (u16)pci_read_config(dev.bus, dev.device, dev.function, 4);
    command_write(&dev, (command | 0x402u) & ~4u);
    if (!prepare_card()) goto fail;
    // Software reset, then clock init.
    reg_set(IWX_CSR_RESET, IWX_RESET_SW_RESET);
    u64 start = time_counter_ns();
    while (time_counter_ns() - start < 5000) __asm__ volatile("pause" ::: "memory");
    if (!apm_init()) goto fail;
    if (!clear_persistence()) goto fail;
    if (!read_mac()) goto fail;
    // Refuse a disabled radio honestly instead of failing later.
    if (reg_read(IWX_CSR_GP_CNTRL) & IWX_GP_HW_RFKILL_SW) goto fail;
    if (!card.firmware_ok || !load_firmware()) goto fail;
    if (!wait_alive()) goto fail;
    card.online = true;
    irq_restore(flags); return true;
fail:
    // Pages stay permanently reserved, including on partial initialization.
    // No caller memory is ever exposed to DMA; never retried.
    irq_restore(flags); return false;
}
bool iwx200_online(void) { return card.online && card.alive; }
void iwx200_mac(u8 out[6]) { if (out) copy(out, card.mac, 6); }
// Stage a firmware image for the next init attempt. Copies nothing; the
// caller keeps the image alive until init completes. False when the card
// was already probed or the image is malformed.
bool iwx200_stage_fw(const void* image, u64 size) {
    if (card.attempted) return false;
    if (!iwx200_parse_fw(image, size, &card.fw)) return false;
    card.firmware_ok = true;
    return true;
}
#define IWX_FW_PATH "/lib/firmware/iwlwifi-cc-a0-71.ucode"
// The blob is 1.3 MiB; bound the read so a corrupt length cannot exhaust
// the heap. The buffer stays permanently allocated for the staged image.
#define IWX_FW_MAX_READ (2u * 1024u * 1024u)
void iwx200_late_init(void) {
    if (card.attempted) return;
    file_t* file;
    if (file_open(IWX_FW_PATH, O_RDONLY, &file) < 0) return;
    u8* image = kmalloc(IWX_FW_MAX_READ);
    if (!image) { file_unref(file); return; }
    u64 used = 0;
    for (;;) {
        i64 n = file_read(file, image + used, IWX_FW_MAX_READ - used);
        if (n < 0) { kfree(image); file_unref(file); return; }
        if (!n) break;
        used += (u64)n;
        if (used == IWX_FW_MAX_READ) break;
    }
    file_unref(file);
    if (!used || !iwx200_stage_fw(image, used)) { kfree(image); return; }
    // The staged image borrows this buffer; it is never freed.
    if (!iwx200_init()) { klog(LOG_WARN, "iwx200: AX200 not ready\n"); return; }
    if (iwx200_run_init()) { klog(LOG_WARN, "iwx200: AX200 init commands failed\n"); return; }
    klog(LOG_OKAY, "iwx200: AX200 firmware alive, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
         card.mac[0], card.mac[1], card.mac[2], card.mac[3], card.mac[4], card.mac[5]);
}
// --- Command transport ---
// A TFD entry: transfer descriptor for one command. Two transfer buffers
// at most: the 8-byte header plus up to 316 bytes of payload.
typedef struct {
    u16 count;
    u8 pad[2];
    struct { u16 length; u16 reserved; u32 address_low; u32 address_high; } tb[2];
    u8 reserved[256 - 4 - 24];
} tfd_t;
_Static_assert(sizeof(tfd_t) == 256, "AX200 TFD size");
// A command buffer: wide header plus inline payload.
typedef struct {
    u8 opcode, group, index, queue;
    u16 length, reserved;
    u8 version;
    u8 payload[IWX_CMD_MAX_PAYLOAD];
} cmd_t;
// An RX notification: firmware header plus payload in a 4 KiB buffer.
typedef struct {
    u32 length_flags;
    u8 code, flags, index, queue;
    u8 payload[];
} notif_t;
static void cmd_put16(volatile u8* p, u16 value) { p[0] = (u8)value; p[1] = (u8)(value >> 8); }
static void cmd_put32(volatile u8* p, u32 value) {
    p[0] = (u8)value; p[1] = (u8)(value >> 8); p[2] = (u8)(value >> 16); p[3] = (u8)(value >> 24);
}
static u32 get32(const volatile u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
// Allocate the TX command ring, command buffers and RX ring. All DMA pages
// stay permanently reserved. The context info points at these; this must
// run before the firmware is kicked.
static bool transport_alloc(void) {
    u64 tx_phys, cmd_phys, free_phys, status_phys;
    u8* tx = dma_contig(&tx_phys, IWX_TX_RING_COUNT * 256);
    u8* cmd = dma_contig(&cmd_phys, IWX_TX_RING_COUNT * 324);
    u8* free_ring = dma_contig(&free_phys, IWX_RX_RING_COUNT * 8);
    u8* status = dma_contig(&status_phys, 16);
    if (!tx || !cmd || !free_ring || !status) return false;
    card.tx_ring = (volatile u8*)tx;
    card.tx_phys = tx_phys;
    card.cmd_data = (volatile u8*)cmd;
    card.cmd_phys = cmd_phys;
    card.rx_free_phys = free_phys;
    card.rx_status_phys = status_phys;
    card.rx_free = (u64*)free_ring;
    card.rx_status = (volatile u16*)status;
    for (u32 i = 0; i < IWX_RX_RING_COUNT; i++) {
        u64 phys;
        u8* buffer = dma_contig(&phys, IWX_RX_BUFFER_SIZE);
        if (!buffer) return false;
        card.rx_buffers[i] = buffer;
        card.rx_free[i] = phys | (i & 0xfff);
    }
    return true;
}
// The firmware consumes RX buffers in order; replenish by handing it the
// next buffer address. Called with IRQs excluded.
static void rx_replenish(u32 index) {
    card.rx_free[index] = ((u64)card.rx_buffers[index] - vmm_hhdm()) | (index & 0xfff);
    fence();
}
// Poll one notification from the RX ring. Returns the payload length, 0
// when no notification is waiting. The buffer is reused after the caller
// copies out; the caller must copy before the next call.
static u32 rx_poll(const volatile u8** payload_out) {
    u16 closed = *card.rx_status;
    if ((closed & 0xfff) >= IWX_RX_RING_COUNT) return 0;
    if (card.rx_next == (closed & 0x1ff)) return 0;
    u32 index = card.rx_next;
    const volatile u8* buffer = card.rx_buffers[index];
    u32 length_flags = get32(buffer);
    if (length_flags == 0x55550000u) return 0; // invalid marker
    u32 length = length_flags & 0x3fff;
    if (length < 8 || length > IWX_RX_BUFFER_SIZE) return 0;
    *payload_out = buffer;
    card.rx_next = (card.rx_next + 1) % IWX_RX_RING_COUNT;
    rx_replenish(index);
    return length;
}
// Send a synchronous command and wait for its response. Polls the RX ring
// with a TSC deadline; never blocks. Matched by slot index.
i32 iwx200_command(u32 group, u32 opcode, u32 version,
                   const void* payload, u32 payload_length,
                   void* response, u32 capacity) {
    if (!card.online) return -22; // EINVAL
    if ((!payload && payload_length) || payload_length > IWX_CMD_MAX_PAYLOAD) return -90; // EMSGSIZE
    if (payload_length && !response && capacity) return -22;
    u64 flags = irq_save();
    u32 slot = card.tx_next;
    u8 index = card.tx_index++;
    volatile u8* cmd = card.cmd_data + (u64)slot * 324;
    cmd[0] = (u8)opcode; cmd[1] = (u8)group; cmd[2] = index; cmd[3] = 0;
    cmd_put16(cmd + 4, (u16)payload_length); cmd[6] = 0; cmd[7] = (u8)version;
    for (u32 i = 0; i < payload_length; i++) cmd[8 + i] = ((const u8*)payload)[i];    // TFD: one transfer buffer covering header plus payload.
    volatile u8* tfd = card.tx_ring + (u64)slot * 256;
    u64 cmd_phys = card.cmd_phys + (u64)slot * 324;
    cmd_put16(tfd, 1); // one TB
    tfd[2] = tfd[3] = 0;
    cmd_put16(tfd + 4, (u16)(8 + payload_length));
    cmd_put16(tfd + 6, 0);
    cmd_put32(tfd + 8, (u32)cmd_phys);
    cmd_put32(tfd + 12, (u32)(cmd_phys >> 32));
    fence();
    card.tx_next = (card.tx_next + 1) % IWX_TX_RING_COUNT;
    reg_write(IWX_HBUS_WRPTR, (IWX_CMD_QUEUE << 16) | card.tx_next);
    fence();
    // Wait for the response with our slot index.
    u64 start = time_counter_ns();
    i32 result = -110; // ETIMEDOUT
    for (;;) {
        const volatile u8* buffer;
        u32 length = rx_poll(&buffer);
        if (length >= 8) {
            u8 code = buffer[4], rflags = buffer[5], rindex = buffer[6], queue = buffer[7];
            u32 payload_length = length - 8;
            // Unsolicited notifications (high queue bit) are recorded for
            // the MLME layer; solicited responses match by slot index.
            // RX MPDUs (0xc1) carry data/management frames: stash the whole
            // notification so mgmt_rx can retrieve it after the command.
            if (queue & 0x80) {
                card.notif_code = code;
                card.notif_group = 0; // legacy group; wide groups decoded later
                card.notif_length = payload_length > sizeof(card.notif_payload) ?
                    sizeof(card.notif_payload) : payload_length;
                for (u32 i = 0; i < card.notif_length; i++)
                    card.notif_payload[i] = buffer[8 + i];
                card.notif_pending = true;
                if (code == IWX_CMD_INIT_COMPLETE) {
                    // Init-complete needs no response; keep polling for
                    // the solicited reply if one is outstanding.
                    if (group == 0 && opcode == 0) { result = 0; break; }
                }
                continue;
            }
            if (rindex == index) {
                if (rflags & IWX_CMD_FAILED) result = -5; // EIO
                else if (payload_length > capacity) result = -90;
                else {
                    for (u32 i = 0; i < payload_length; i++)
                        ((u8*)response)[i] = buffer[8 + i];
                    result = (i32)payload_length;
                }
                break;
            }
            // A response for another outstanding slot: ignore, keep polling.
        }
        if (time_counter_ns() - start >= IWX_CMD_NS) break;
        __asm__ volatile("pause" ::: "memory");
    }
    irq_restore(flags);
    return result;
}
// --- Post-alive init sequence ---
// Extended config (NVM mode), NVM access complete, then NVM info for the
// factory MAC. Little-endian command payloads built inline.
static void put32le(u8* p, u32 value) {
    p[0] = (u8)value; p[1] = (u8)(value >> 8); p[2] = (u8)(value >> 16); p[3] = (u8)(value >> 24);
}
static u8 nvm_mac[6];
i32 iwx200_run_init(void) {
    if (!card.online) return -22;
    u8 payload[8];
    u8 response[512];
    // INIT_EXTENDED_CFG with the NVM flag.
    put32le(payload, IWX_SYS_INIT_NVM);
    if (iwx200_command(IWX_GRP_SYSTEM, IWX_SYS_INIT_EXTENDED_CFG, 0, payload, 4, response, sizeof(response)) < 0)
        return -5;
    // NVM_ACCESS_COMPLETE, zeroed.
    put32le(payload, 0);
    if (iwx200_command(IWX_GRP_REGULATORY, IWX_REG_NVM_ACCESS_COMPLETE, 0, payload, 4, response, sizeof(response)) < 0)
        return -5;
    // INIT_COMPLETE arrives as an unsolicited notification, consumed by
    // the transport during the commands above. Check the recorded flag
    // first, then poll once more in case it arrives late.
    bool complete = card.notif_pending && card.notif_code == IWX_CMD_INIT_COMPLETE;
    card.notif_pending = false;
    for (u32 attempt = 0; !complete && attempt < 2; attempt++) {
        u64 flags = irq_save();
        u64 start = time_counter_ns();
        while (time_counter_ns() - start < IWX_CMD_NS) {
            const volatile u8* buffer;
            if (rx_poll(&buffer) >= 8 && (buffer[7] & 0x80) && buffer[4] == IWX_CMD_INIT_COMPLETE)
                { complete = true; break; }
            __asm__ volatile("pause" ::: "memory");
        }
        irq_restore(flags);
    }
    if (!complete) return -110;
    put32le(payload, 0);
    i32 n = iwx200_command(IWX_GRP_REGULATORY, IWX_REG_NVM_GET_INFO, 0, payload, 4, response, sizeof(response));
    if (n < 8) return -110;
    // NVM general section: flags(4) version(2) board(1) addrs(1).
    u8 addrs = response[7];
    if (!addrs) return -5;
    // The factory MAC comes from the strap registers (already validated
    // during probe); NVM confirms an address exists for this card.
    copy(nvm_mac, card.mac, 6);
    return 0;
}
void iwx200_nvm_mac(u8 out[6]) { if (out) copy(out, nvm_mac, 6); }
// --- Wireless data path ---
// Station MAC context: BSS client mode. Field order per the firmware API.
static i32 mac_context(u8 action, const u8 bssid[6], bool associated, u16 aid) {
    u8 cmd[128];
    for (u32 i = 0; i < sizeof(cmd); i++) cmd[i] = 0;
    put32le(cmd, 0); // id and color
    put32le(cmd + 4, action); // 1 add, 2 modify, 3 remove
    put32le(cmd + 8, 5); // BSS_STA
    put32le(cmd + 12, 0); // tsf id A
    copy(cmd + 16, card.mac, 6); // node address
    copy(cmd + 24, bssid, 6);
    put32le(cmd + 32, 0x0000000f); // CCK rates 1/2/5.5/11
    put32le(cmd + 36, 0x00000ff5); // OFDM rates
    put32le(cmd + 44, 0x20); // short preamble
    put32le(cmd + 48, 0x10); // short slot
    put32le(cmd + 52, (1u << 2) | (associated ? 0 : (1u << 6))); // accept group + beacons pre-assoc
    // mac_data_sta past the QoS/AC blocks (offset 96 in this layout).
    put32le(cmd + 96, associated ? 1 : 0); // is_assoc
    put32le(cmd + 116, 10); // listen interval
    put32le(cmd + 120, aid); // assoc id
    u8 response[16];
    i32 n = iwx200_command(IWX_GRP_MAC_CONF, 0x28, 0, cmd, sizeof(cmd), response, sizeof(response));
    return n < 0 ? n : 0;
}
// Station entry for the AP: link-layer address plus HT/VHT capability flags.
static i32 add_station(const u8 bssid[6], bool modify) {
    u8 cmd[64];
    for (u32 i = 0; i < sizeof(cmd); i++) cmd[i] = 0;
    cmd[0] = modify ? 1 : 0;
    put32le(cmd + 4, 0); // mac id and color
    copy(cmd + 8, bssid, 6);
    cmd[16] = 0; // station id 0
    // station flags: FAT 80 MHz (2 << 26), MIMO2 (1 << 28).
    put32le(cmd + 20, (2u << 26) | (1u << 28));
    u8 response[16];
    i32 n = iwx200_command(IWX_GRP_LEGACY, 0x18, 0, cmd, sizeof(cmd), response, sizeof(response));
    return n < 0 ? n : 0;
}
// Install the pairwise CCMP key for station 0 (ADD_STA_KEY, legacy 0x17).
static i32 install_key(const u8 tk[16]) {
    u8 cmd[64];
    for (u32 i = 0; i < sizeof(cmd); i++) cmd[i] = 0;
    cmd[0] = 0; // station id
    cmd[1] = 0; // pairwise offset
    cmd[2] = 2; cmd[3] = 0; // CCMP key flags
    copy(cmd + 4, tk, 16);
    u8 response[16];
    i32 n = iwx200_command(IWX_GRP_LEGACY, 0x17, 0, cmd, sizeof(cmd), response, sizeof(response));
    return n < 0 ? n : 0;
}
// Session protection around the association exchange (900 TU).
static i32 session_protect(bool add) {
    u8 cmd[24];
    for (u32 i = 0; i < sizeof(cmd); i++) cmd[i] = 0;
    put32le(cmd + 4, add ? 1 : 3);
    put32le(cmd + 8, 0); // assoc conf id
    put32le(cmd + 12, 900); // duration
    u8 response[16];
    i32 n = iwx200_command(IWX_GRP_MAC_CONF, 0x05, 0, cmd, sizeof(cmd), response, sizeof(response));
    return n < 0 ? n : 0;
}
// Link state and association context for the network layer.
static u8 link_state = IWX_LINK_DOWN;
static u8 link_bssid[6];
static wifi_sta_t mlme;
static wpa_t handshake;
iwx_link_t iwx200_link(void) { return card.online ? link_state : IWX_LINK_DOWN; }
// Management frame transmit through the firmware's auxiliary queue. The
// frame is built by the MLME core (wifi_next_tx) and sent as a TX command
// on the management queue; the reply arrives as an RX notification.
static i32 mgmt_tx(const void* frame, u32 length) {
    if (length > 256 || !length) return -90;
    u8 cmd[8 + 256];
    // TX_CMD gen2 prefix: length, offload, flags (rate-controlled mgmt),
    // DRAM info, rate flags zero (firmware rate control for data only).
    cmd[0] = (u8)(length + 8); cmd[1] = (u8)((length + 8) >> 8);
    cmd[2] = 24; cmd[3] = 0; // offload: MAC header size
    put32le(cmd + 4, 0x01); // CMD_RATE: fixed legacy rate for mgmt
    for (u32 i = 0; i < length; i++) cmd[8 + i] = ((const u8*)frame)[i];
    u8 response[16];
    // Data-path TX commands travel the same queue-0 transport until the
    // SCD data queues are configured on hardware.
    i32 n = iwx200_command(IWX_GRP_LEGACY, 0x1c, 0, cmd, 8 + length, response, sizeof(response));
    return n < 0 ? n : 0;
}
// Poll one RX management frame into the buffer. Returns the length, 0
// when nothing arrives before the deadline.
static u32 mgmt_rx(void* buffer, u32 capacity, u64 nanoseconds) {
    // A command wait may have already consumed the frame into the stash.
    if (card.notif_pending && card.notif_code == 0xc1 && card.notif_length > 16) {
        u32 frame_length = card.notif_length - 16;
        if (frame_length <= capacity) {
            for (u32 i = 0; i < frame_length; i++)
                ((u8*)buffer)[i] = card.notif_payload[16 + i];
            card.notif_pending = false;
            return frame_length;
        }
    }
    u64 start = time_counter_ns();
    for (;;) {
        const volatile u8* notif;
        u32 length = rx_poll(&notif);
        // RX MPDU notifications carry 802.11 frames past an 8-byte header
        // plus a 16-byte descriptor; management frames route to the MLME.
        if (length > 24) {
            u32 frame_length = length - 24;
            if (frame_length <= capacity) {
                for (u32 i = 0; i < frame_length; i++)
                    ((u8*)buffer)[i] = notif[24 + i];
                return frame_length;
            }
        }
        if (time_counter_ns() - start >= nanoseconds) return 0;
        __asm__ volatile("pause" ::: "memory");
    }
}
i32 iwx200_connect(const void* ssid, u32 ssid_length, const char* passphrase) {
    if (!card.online) return -22;
    if (!ssid || !ssid_length || ssid_length > WIFI_SSID_MAX) return -22;
    // The BSSID comes from a prior scan; without scan results there is no
    // target. Scanning needs the UMAC_SCAN command, verified on hardware.
    // Until then, association targets the configured BSSID only.
    if (!link_bssid[0] && !link_bssid[1] && !link_bssid[2] &&
        !link_bssid[3] && !link_bssid[4] && !link_bssid[5]) return -95;
    wifi_sta_init(&mlme, card.mac);
    if (!wifi_sta_join(&mlme, link_bssid, ssid, ssid_length)) return -22;
    link_state = IWX_LINK_ASSOCIATING;
    if (session_protect(true)) goto fail;
    if (mac_context(1, link_bssid, false, 0)) goto fail;
    if (add_station(link_bssid, false)) goto fail;
    // Open-system authentication + association through the MLME core.
    u8 frame[512];
    for (u32 round = 0; round < 4; round++) {
        u32 tx_length = wifi_next_tx(&mlme);
        if (!tx_length) break;
        if (mgmt_tx(mlme.last_tx, tx_length)) goto fail;
        u32 rx_length = mgmt_rx(frame, sizeof(frame), 1000000000ULL);
        if (!rx_length || !wifi_receive(&mlme, frame, rx_length)) goto fail;
        if (mlme.state == WIFI_ASSOCIATED) break;
    }
    if (mlme.state != WIFI_ASSOCIATED) goto fail;
    if (mac_context(2, link_bssid, true, mlme.aid)) goto fail;
    if (add_station(link_bssid, true)) goto fail;
    session_protect(false);
    // Open network: link is up with no keys.
    if (!passphrase || !passphrase[0]) { link_state = IWX_LINK_UP; return 0; }
    // WPA2-PSK: run the 4-way handshake. EAPOL frames are 802.11 data to
    // the AP's MAC with LLC/SNAP encapsulation (RFC 1042).
    link_state = IWX_LINK_HANDSHAKE;
    wpa_derive_pmk(&handshake, passphrase, ssid, ssid_length);
    wpa_start(&handshake, link_bssid, card.mac);
    u8 eapol[512], data[1536], reply[512];
    for (u32 round = 0; round < 8; round++) {
        u32 rx_length = mgmt_rx(data, sizeof(data), 2000000000ULL);
        if (!rx_length) goto fail;
        // Strip the 802.11 data header (24 bytes) and LLC/SNAP (8 bytes)
        // to reach the EAPOL PDU.
        if (rx_length < 32) continue;
        u32 reply_length = 0;
        if (!wpa_receive(&handshake, data + 32, rx_length - 32, reply, sizeof(reply), &reply_length))
            continue;
        if (wpa_completed(&handshake)) break;
        if (!reply_length) continue;
        // Encapsulate the reply: data header + LLC/SNAP + EAPOL.
        u8* tx = eapol;
        tx[0] = 0x08; tx[1] = 0x01; // data, to-DS
        tx[2] = tx[3] = 0;
        copy(tx + 4, link_bssid, 6); copy(tx + 10, card.mac, 6); copy(tx + 16, link_bssid, 6);
        tx[22] = 0; tx[23] = 0;
        static const u8 snap[8] = { 0xaa, 0xaa, 0x03, 0, 0, 0, 0x88, 0x8e };
        copy(tx + 24, snap, 8);
        copy(tx + 32, reply, reply_length);
        if (mgmt_tx(tx, 32 + reply_length)) goto fail;
    }
    if (!wpa_completed(&handshake)) goto fail;
    // Install the pairwise key, then report the link up.
    u8 tk[16]; wpa_tk(&handshake, tk);
    if (install_key(tk)) goto fail;
    link_state = IWX_LINK_UP;
    return 0;
fail:
    session_protect(false);
    link_state = IWX_LINK_DOWN;
    return -5;
}
// Explicit BSSID for association (from a scan; the UMAC scan command
// arrives with hardware verification). Returns false on a bad address.
bool iwx200_set_bssid(const u8 bssid[6]) {
    if (!bssid) return false;
    u8 any = 0; for (u32 i = 0; i < 6; i++) any |= bssid[i];
    if (!any || (bssid[0] & 1)) return false;
    copy(link_bssid, bssid, 6);
    return true;
}
// Transmit one Ethernet frame: 802.11 data header, CCMP protection with
// the pairwise key and a fresh packet number, then the TX command.
bool iwx200_send(const void* frame, u32 length) {
    if (!card.online || link_state != IWX_LINK_UP || !frame) return false;
    if (length < 14 || length > 1500) return false;
    const u8* eth = frame;
    u8 data[1600];
    // 802.11 data header: to-DS, addresses BSSID/station/BSSID.
    data[0] = 0x08; data[1] = 0x01;
    data[2] = data[3] = 0;
    copy(data + 4, link_bssid, 6); copy(data + 10, card.mac, 6); copy(data + 16, link_bssid, 6);
    data[22] = 0; data[23] = 0;
    u8* payload = data + 24;
    u32 payload_length;
    if (wpa_completed(&handshake)) {
        // CCMP header: PN bytes plus key id 0.
        u8 tk[16]; wpa_tk(&handshake, tk);
        u8 pn[6];
        u64 pn_value = ++handshake.tx_pn;
        for (u32 i = 0; i < 6; i++) pn[i] = (u8)(pn_value >> (i * 8));
        payload[0] = pn[0]; payload[1] = pn[1]; payload[2] = 0x20;
        payload[3] = pn[2]; payload[4] = pn[3]; payload[5] = pn[4]; payload[6] = pn[5]; payload[7] = 0;
        // AAD: masked frame control, A1, A2, A3, sequence (22 bytes).
        u8 aad[28];
        aad[0] = 0x08; aad[1] = 0x41; // protected bit set for the MIC
        copy(aad + 2, link_bssid, 6); copy(aad + 8, card.mac, 6); copy(aad + 14, link_bssid, 6);
        aad[20] = 0; aad[21] = 0;
        u8 llc[8] = { 0xaa, 0xaa, 0x03, 0, 0, 0, eth[12], eth[13] };
        u8 plain[1520];
        copy(plain, llc, 8); copy(plain + 8, eth + 14, length - 14);
        u32 protected_length = 0;
        if (!wifi_ccmp_encrypt(tk, pn, card.mac, aad, 22, plain, length - 14 + 8,
                               payload + 8, &protected_length)) return false;
        payload_length = 8 + protected_length;
    } else {
        // Open network: LLC/SNAP encapsulation, no protection.
        static const u8 snap[6] = { 0xaa, 0xaa, 0x03, 0, 0, 0 };
        copy(payload, snap, 6);
        payload[6] = eth[12]; payload[7] = eth[13];
        copy(payload + 8, eth + 14, length - 14);
        payload_length = length - 14 + 8;
    }
    return !mgmt_tx(data, 24 + payload_length);
}
// Receive one Ethernet frame: strip the 802.11 header, verify CCMP,
// convert LLC/SNAP to Ethernet. Returns the length, 0 when empty.
u32 iwx200_receive(void* frame, u32 capacity) {
    if (!card.online || link_state != IWX_LINK_UP || !frame) return 0;
    u8 data[1600];
    u32 rx_length = mgmt_rx(data, sizeof(data), 0);
    if (rx_length < 32) return 0;
    if (data[0] != 0x08 || !(data[1] & 0x02)) return 0; // data, from-DS
    const u8* payload = data + 24;
    u32 payload_length = rx_length - 24;
    u8 plain[1600];
    u32 plain_length = 0;
    if (wpa_completed(&handshake)) {
        if (payload_length < 16) return 0;
        // CCMP header PN; reject replays.
        u64 pn = (u64)payload[0] | ((u64)payload[1] << 8) | ((u64)payload[3] << 16) |
                 ((u64)payload[4] << 24) | ((u64)payload[5] << 32) | ((u64)payload[6] << 40);
        if (pn <= handshake.rx_pn) return 0;
        u8 pn_bytes[6];
        for (u32 i = 0; i < 6; i++) pn_bytes[i] = (u8)(pn >> (i * 8));
        u8 aad[28];
        aad[0] = 0x08; aad[1] = 0x41;
        copy(aad + 2, data + 4, 6); copy(aad + 8, data + 16, 6); copy(aad + 14, data + 10, 6);
        aad[20] = data[22]; aad[21] = data[23];
        u8 tk[16]; wpa_tk(&handshake, tk);
        if (!wifi_ccmp_decrypt(tk, pn_bytes, data + 16, aad, 22, payload + 8,
                               payload_length - 8, plain, &plain_length)) return 0;
        handshake.rx_pn = pn;
    } else {
        // Open network: payload starts with LLC/SNAP directly.
        if (payload_length < 8) return 0;
        plain_length = payload_length;
        copy(plain, payload, plain_length);
    }
    // LLC/SNAP to Ethernet: destination, source, ethertype, payload.
    if (plain_length < 8 || plain[0] != 0xaa || plain[1] != 0xaa || plain[2] != 0x03) return 0;
    u32 eth_length = 14 + (plain_length - 8);
    if (eth_length > capacity) return 0;
    u8* out = frame;
    copy(out, data + 4, 6); copy(out + 6, data + 16, 6);
    out[12] = plain[6]; out[13] = plain[7];
    copy(out + 14, plain + 8, plain_length - 8);
    return eth_length;
}
