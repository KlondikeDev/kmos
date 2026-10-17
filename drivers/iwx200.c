// This file has been generated in whole or in part by a Large Language Model
// Intel Wi-Fi 6 AX200 bring-up: PCI probe, CSR init, persistence clearing,
// MAC address, INIT firmware load and the alive handshake. RX/TX queues and
// the 802.11 state machine arrive later; this file only takes the card from
// cold to firmware-alive. Everything runs in the caller's context with IRQs
// excluded, like the e1000 driver: no blocking, no scheduler calls, every
// hardware wait bounded by the TSC clock.
#include <iwx200.h>
#include <pci.h>
#include <memory.h>
#include <ktime.h>
#include <io.h>

#define MMIO_BASE 0xffffc42000000000ULL
#define MMIO_SIZE 0x20000u

// Gen2 context info prefix: version, control, receive/command ring
// addresses, then the INIT image tables. Offsets match the firmware's
// layout (version@0, control@8, rbd@24, hcmd@48, umac@64, lmac@128).
typedef struct {
    u16 mac_id, version, size, reserved_ver;
    u32 control_flags, reserved_ctrl;
    u64 reserved0;
    u64 rx_free, rx_used, rx_status;
    u64 cmd_addr;
    u16 cmd_size, reserved_cmd;
    u8 reserved_a[4];
    u64 umac_image[8];
    u64 lmac_image[8];
} ctxt_t;
_Static_assert(sizeof(ctxt_t) == 192, "AX200 gen2 context prefix size");

static struct {
    volatile u8* regs;
    u8 mac[6];
    bool attempted, online, alive;
    bool firmware_ok;
    iwx200_fw_t fw;
    u64 ctxt_phys;
} card;

static u32 reg_read(u32 offset) { return *(volatile u32*)(card.regs + offset); }
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
    u64 offset = 104;
    while (offset + 8 <= size) {
        u32 type = (u32)p[offset] | ((u32)p[offset+1] << 8) | ((u32)p[offset+2] << 16) | ((u32)p[offset+3] << 24);
        u32 length = (u32)p[offset+4] | ((u32)p[offset+5] << 8) | ((u32)p[offset+6] << 16) | ((u32)p[offset+7] << 24);
        offset += 8;
        if (length > size - offset) return false;
        if (type == IWX_TLV_SEC_INIT && length >= 4) {
            // INIT payload: consecutive (device offset, bytes) pairs. Each
            // pair is a dword offset followed by data running to the next
            // separator marker or the TLV end. Scan dword-aligned: payload
            // bytes can contain marker-like values at odd alignments.
            u64 end = offset + length, cursor = offset;
            while (cursor + 4 <= end) {
                u32 marker = (u32)p[cursor] | ((u32)p[cursor+1] << 8) |
                             ((u32)p[cursor+2] << 16) | ((u32)p[cursor+3] << 24);
                cursor += 4;
                if (marker == IWX_SEP_CPU12 || marker == IWX_SEP_PAGING) continue;
                if (out->init_count >= IWX_UCODE_SECT_MAX) return false;
                u64 next = cursor;
                while (next + 4 <= end) {
                    u32 candidate = (u32)p[next] | ((u32)p[next+1] << 8) |
                                    ((u32)p[next+2] << 16) | ((u32)p[next+3] << 24);
                    if (candidate == IWX_SEP_CPU12 || candidate == IWX_SEP_PAGING) break;
                    next += 4;
                }
                // Trailing bytes before the TLV end belong to this section.
                out->init[out->init_count].address = (u64)(p + cursor);
                out->init[out->init_count].device_offset = marker;
                out->init[out->init_count].length = (u32)(next - cursor);
                out->init_count++;
                cursor = next;
            }
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
    return out->init_count > 0;
}
// Load a staged INIT image: copy sections into DMA pages, describe them in
// context info, point the card at it and kick the CPU. The image must have
// been parsed by iwx200_parse_fw; pages stay permanently reserved.
static bool load_firmware(void) {
    if (!card.firmware_ok || !card.fw.init_count) return false;
    u64 ctxt_phys;
    ctxt_t* ctxt = dma_page(&ctxt_phys);
    if (!ctxt) return false;
    card.ctxt_phys = ctxt_phys;
    ctxt->mac_id = (u16)(reg_read(IWX_CSR_HW_REV) & 0xffff);
    ctxt->version = 0;
    ctxt->size = sizeof(*ctxt) / 4;
    ctxt->control_flags = IWX_CTXT_TFD_LONG | (9u << IWX_CTXT_RB_CB_POS) |
                          (IWX_CTXT_RB_4K << IWX_CTXT_RB_SIZE_POS);
    u32 lmac = 0;
    for (u32 i = 0; i < card.fw.init_count && lmac < 8; i++) {
        u32 remaining = card.fw.init[i].length, done = 0;
        u64 first_phys = 0;
        // Sections may exceed one page; chain pages but only the first
        // address enters the context table (INIT sections are small).
        while (remaining) {
            u64 phys;
            u8* page = dma_page(&phys);
            if (!page) return false;
            u32 chunk = remaining > 4096 ? 4096 : remaining;
            copy(page, (const void*)(card.fw.init[i].address + done), chunk);
            if (!done) first_phys = phys;
            done += chunk; remaining -= chunk;
        }
        // Device offsets below 2 MiB address LMAC; the table order is fixed.
        ctxt->lmac_image[lmac++] = first_phys;
    }
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
