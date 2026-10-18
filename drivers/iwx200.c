// This file has been generated in whole or in part by a Large Language Model
// Register/DMA ABI and initialization ordering adapted from OpenBSD iwx.
/*
 * Copyright (c) 2014, 2016 genua gmbh <info@genua.de>
 *   Author: Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2014 Fixup Software Ltd.
 * Copyright (c) 2017, 2019, 2020 Stefan Sperling <stsp@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*-
 * Based on BSD-licensed source modules in the Linux iwlwifi driver,
 * which were used as the reference documentation for this implementation.
 *
 ******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************
 */

/*-
 * Copyright (c) 2007-2010 Damien Bergamini <damien.bergamini@free.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

// Intel Wi-Fi 6 AX200 bring-up: PCI probe, CSR init, persistence clearing,
// MAC address, firmware self-load, RX notifications and command transport.
// Hardware waits use bounded polling with interrupts enabled. DMA allocations
// remain quarantined on failure. Physical AX200 validation is still required.
#include <iwx200.h>
#include <pci.h>
#include <memory.h>
#include <ktime.h>
#include <io.h>
#include <file.h>
#include <klog.h>

#define MMIO_BASE 0xffffc42000000000ULL
#define MMIO_SIZE 0x4000u

// Intel gen2 context ABI, OpenBSD if_iwxreg.h iwx_context_info.
// Every image table has 64 slots; the reserved blocks are part of the ABI.
typedef struct __attribute__((packed)) {
    u16 mac_id, version, size, reserved_ver;
    u32 control_flags, reserved_ctrl;
    u64 reserved0;
    u64 rx_free, rx_used, rx_status;
    u64 cmd_addr;
    u8 cmd_size, reserved_cmd[7];
    u32 reserved1[4];
    u8 dump_cfg[16], edbg_cfg[16], pnvm_cfg[16];
    u32 reserved2[16];
    u64 umac_image[64], lmac_image[64], paging_image[64];
    u32 reserved3[16];
} ctxt_t;
_Static_assert(sizeof(ctxt_t) == 1792, "Intel gen2 context ABI");
_Static_assert(__builtin_offsetof(ctxt_t, rx_free) == 24, "context RX offset");
_Static_assert(__builtin_offsetof(ctxt_t, cmd_addr) == 48, "context CMD offset");
_Static_assert(__builtin_offsetof(ctxt_t, umac_image) == 192, "context DRAM offset");
_Static_assert(__builtin_offsetof(ctxt_t, lmac_image) == 704, "context LMAC offset");
_Static_assert(__builtin_offsetof(ctxt_t, paging_image) == 1216, "context paging offset");

static struct {
    volatile u8* regs;
    u8 mac[6];
    bool attempted, online, alive;
    bool firmware_ok, command_busy, init_complete, rx_started, radio_ready;
    pci_device_t pci;
    u16 pci_command;
    iwx200_status_t status;
    u64 rx_used_phys;
    u32 rx_offset;
    u8 rx_packet[IWX_RX_BUFFER_SIZE];
    iwx200_notification_fn notification;
    iwx200_nvm_t nvm;
    struct {
        bool allocated, active;
        u8 qid, station, tid;
        u16 head, tail, pending;
        u8* ring;
        u16* byte_counts;
        u64 ring_phys, byte_counts_phys;
        u8* buffers[IWX_TX_RING_COUNT];
        u64 addresses[IWX_TX_RING_COUNT];
    } data[4];
    iwx200_fw_t fw;
    u64 ctxt_phys;
    // Command transport: DMA rings plus host-side mirrors.
    volatile u8* tx_ring; // 256 TFDs of 256 bytes
    u8* cmd_buffers[IWX_TX_RING_COUNT];
    u64 cmd_addresses[IWX_TX_RING_COUNT];
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

#ifdef IWX200_TEST_MMIO
static u32 reg_read(u32 offset) { return iwx_test_read(offset); }
#else
static u32 reg_read(u32 offset) { return *(volatile u32*)(card.regs + offset); }
#endif
static bool transport_alloc(void);
static bool data_completion(const volatile u8* packet, u32 total);
#ifdef IWX200_TEST_MMIO
static void reg_write(u32 offset, u32 value) { iwx_test_write(offset, value); }
#else
static void reg_write(u32 offset, u32 value) { *(volatile u32*)(card.regs + offset) = value; }
#endif
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
// Enable Memory Space and Bus Master on every PCI-PCI bridge whose bus range
// contains the card, as Linux pci_enable_bridge() does. Firmware often
// leaves bus mastering off on root ports of slots it never used (WiFi); the
// card's MMIO then works but every DMA it issues is rejected upstream.
static void bridges_enable(u8 target_bus) {
    card.status.bridges = 0;
    for (u32 bus = 0; bus < 256; bus++)
        for (u32 dev = 0; dev < 32; dev++)
            for (u32 fn = 0; fn < 8; fn++) {
                u32 id = pci_read_config((u8)bus, (u8)dev, (u8)fn, 0);
                if ((id & 0xffff) == 0xffff || !(id & 0xffff)) { if (!fn) break; continue; }
                u32 header = (pci_read_config((u8)bus, (u8)dev, (u8)fn, 0x0c) >> 16) & 0xff;
                if ((header & 0x7f) == 1) {
                    u32 buses = pci_read_config((u8)bus, (u8)dev, (u8)fn, 0x18);
                    u8 secondary = (u8)(buses >> 8), subordinate = (u8)(buses >> 16);
                    if (secondary <= target_bus && target_bus <= subordinate) {
                        u16 command = (u16)pci_read_config((u8)bus, (u8)dev, (u8)fn, 4);
                        if (secondary == target_bus) {
                            card.status.bridge_bdf = (bus << 8) | (dev << 3) | fn;
                            card.status.bridge_command = command;
                        }
                        pci_device_t bridge = {.bus = (u8)bus, .device = (u8)dev, .function = (u8)fn};
                        command_write(&bridge, command | 0x6u);
                        card.status.bridges++;
                    }
                }
                if (!fn && !(header & 0x80)) break;
            }
}
// Clear the PCI retry-timeout byte (config 0x41), as OpenBSD iwx_attach
// does, so PCI Tx retries do not interfere with C3 CPU state.
static void retry_timeout_clear(pci_device_t* dev) {
    outl(PCI_CONFIG_ADDRESS, 0x80000040u | ((u32)dev->bus << 16) |
         ((u32)dev->device << 11) | ((u32)dev->function << 8));
    outb(PCI_CONFIG_DATA + 1, 0);
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
    reg_write(IWX_HBUS_PRPH_WADDR, (address & 0x000fffffu) | (3u << 24));
    fence();
    reg_write(IWX_HBUS_PRPH_WDATA, value);
    fence();
}
static u32 prph_read(u32 address) {
    reg_write(IWX_HBUS_PRPH_RADDR, (address & 0x000fffffu) | (3u << 24));
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
    u32 debug = prph_read(IWX_HPM_DEBUG);
    bool ok = true;
    if (debug != 0xa5a5a5a0u && (debug & IWX_HPM_PERSISTENCE)) {
        if (prph_read(IWX_PREG_WPROT_22000) & IWX_WPROT_LOCKED) ok = false;
        else prph_write(IWX_HPM_DEBUG, debug & ~IWX_HPM_PERSISTENCE);
    }
    return ok;
}
// The MAC address ships in strap registers off the MAC CSR base, most
// significant byte first. Reject the unprogrammed and multicast values.
static bool valid_mac(void) {
    u8 any = 0; for (u32 i = 0; i < 6; i++) any |= card.mac[i];
    static const u8 reserved[6] = {0x02,0xcc,0xaa,0xff,0xee,0x00};
    bool same = true; for (u32 i = 0; i < 6; i++) if (card.mac[i] != reserved[i]) same = false;
    return any && !(card.mac[0] & 1) && !same;
}
static bool read_mac(void) {
    if (!nic_lock()) return false;
    bool ok = false;
    for (u32 attempt = 0; attempt < 2; attempt++) {
        u32 offset = attempt ? 0 : IWX_MAC_STRAP0;
        u32 low = reg_read(IWX_CSR_MAC_CSR_BASE + offset);
        u32 high = reg_read(IWX_CSR_MAC_CSR_BASE + offset + 4);
        if (low == ~0u || high == ~0u) continue;
        card.mac[0] = (u8)(low >> 24); card.mac[1] = (u8)(low >> 16);
        card.mac[2] = (u8)(low >> 8); card.mac[3] = (u8)low;
        card.mac[4] = (u8)(high >> 8); card.mac[5] = (u8)high;
        if (valid_mac()) { ok = true; break; }
    }
    reg_write(IWX_CSR_GP_CNTRL, reg_read(IWX_CSR_GP_CNTRL) & ~IWX_GP_MAC_ACCESS_REQ);
    return ok;
}
// Parse the TLV container; AX200 self-loads the REGULAR image.
bool iwx200_parse_fw(const void* image, u64 size, iwx200_fw_t* out) {
    if (!image || !out || size < 88 + 8) return false;
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
        } else if (type == 30 && length == 8) {
            u32 index = (u32)p[offset] | ((u32)p[offset+1] << 8) |
                        ((u32)p[offset+2] << 16) | ((u32)p[offset+3] << 24);
            if (index < 8) out->capabilities[index] = (u32)p[offset+4] |
                ((u32)p[offset+5] << 8) | ((u32)p[offset+6] << 16) | ((u32)p[offset+7] << 24);
        } else if (type == 23 && length == 4) {
            out->phy_config = (u32)p[offset] | ((u32)p[offset+1] << 8) |
                              ((u32)p[offset+2] << 16) | ((u32)p[offset+3] << 24);
        } else if ((type == 54 || type == 55) && length >= 4) {
            // UMAC/LMAC debug addresses; the first dword is the SRAM address
            // of that core's error log. Top two bits are cache control.
            u32 address = ((u32)p[offset] | ((u32)p[offset+1] << 8) |
                           ((u32)p[offset+2] << 16) | ((u32)p[offset+3] << 24)) & 0x3fffffffu;
            if (type == 55) out->lmac_error_table = address;
            else out->umac_error_table = address;
        } else if (type == IWX_TLV_CMD_VERSIONS) {
            if (length % 4 || length / 4 > 256) return false;
            out->command_version_count = length / 4;
            copy(out->command_versions, p + offset, length);
            for (u32 i = 0; i < length; i += 4) {
                const u8* row = p + offset + i;
                if (row[0] == 1 && row[1] == 0) out->alive_version = row[3];
                if (row[0] == 2 && row[1] == 12) out->nvm_version = row[3];
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
        u64 padded = ((u64)length + 3) & ~3ULL;
        if (padded > size - offset) return false;
        offset += padded;
    }
    return offset == size && (out->init_count > 0 || out->regular_count > 0);
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
    // RX free list, distinct hardware completion list and status pointer
    // for the firmware's receive DMA; command queue address and size.
    ctxt->rx_free = card.rx_free_phys;
    ctxt->rx_used = card.rx_used_phys;
    ctxt->rx_status = card.rx_status_phys;
    ctxt->cmd_addr = card.tx_phys;
    ctxt->cmd_size = 5; // CB_SIZE encoding for 256 entries

    u32 lmac = 0, umac = 0, paging = 0, group = 0;
    for (u32 i = 0; i < card.fw.regular_count; i++) {
        u32 device_offset = card.fw.regular[i].device_offset;
        // Separator entries advance the group without consuming a page.
        if (device_offset == IWX_SEP_CPU12) { group = 1; continue; }
        if (device_offset == IWX_SEP_PAGING) { group = 2; continue; }
        u32 table_offset = group == 0 ? 704 : group == 1 ? 192 : 1216;
        u32* count = group == 0 ? &lmac : group == 1 ? &umac : &paging;
        u32 limit = 64;
        if (*count >= limit) return false;
        u64 section_phys;
        u8* section = dma_contig(&section_phys, card.fw.regular[i].length);
        if (!section) return false;
        copy(section, (const void*)card.fw.regular[i].address, card.fw.regular[i].length);
        copy((u8*)ctxt + table_offset + (*count)++ * 8, &section_phys, 8);
    }
    if (!lmac || !umac) return false;
    fence();
    card.status.stage = IWX200_FW_LOAD;
    // All DMA targets are owned, zeroed, and described before bus mastering.
    // INTx stays enabled: OpenBSD clears INTERRUPT_DISABLE when MSI(-X) is
    // unused ("hardware bug workaround"). We poll, but the card needs it.
    command_write(&card.pci, (card.pci_command | 0x006u) & ~0x401u);
    reg_write(IWX_CSR_CTXT_INFO_BA, (u32)ctxt_phys);
    reg_write(IWX_CSR_CTXT_INFO_BA + 4, (u32)(ctxt_phys >> 32));
    if (!nic_lock()) return false;
    // 250 usec latency tolerance while the firmware self-loads.
    reg_write(IWX_CSR_LTR_LONG_VAL, 0x82FA82FAu);
    prph_write(IWX_UREG_CPU_INIT_RUN, 1);
    reg_write(IWX_CSR_GP_CNTRL, reg_read(IWX_CSR_GP_CNTRL) & ~IWX_GP_MAC_ACCESS_REQ);
    fence();
    return true;
}
static u32 rx_poll(const volatile u8** payload_out);
// Read dwords of device SRAM through the targeted-memory window. The window
// is held only for the read. Returns false when MAC access cannot be granted.
static bool sram_read(u32 address, u32* out, u32 dwords) {
    if (!nic_lock()) return false;
    reg_write(IWX_HBUS_TARG_RADDR, address);
    for (u32 i = 0; i < dwords; i++) out[i] = reg_read(IWX_HBUS_TARG_MEM_RDAT);
    reg_write(IWX_CSR_GP_CNTRL, reg_read(IWX_CSR_GP_CNTRL) & ~IWX_GP_MAC_ACCESS_REQ);
    return true;
}
// Snapshot the firmware's own error logs after a HW/SW error. The firmware
// is dead at this point, so the logs are the only record of why. Offsets
// follow the OpenBSD iwx_error_event_table and iwx_umac_error_event_table
// layouts (valid, error_id, ...; LMAC log_pc is dword 20).
#define IWX_SRAM_MIN_BASE 0x400000u
static void capture_error_log(void) {
    u32 table[24];
    card.status.error_log_read = false;
    card.status.lmac_valid = card.status.lmac_error_id = 0;
    card.status.lmac_log_pc = card.status.umac_error_id = 0;
    if (card.fw.lmac_error_table >= IWX_SRAM_MIN_BASE &&
        sram_read(card.fw.lmac_error_table, table, 24)) {
        card.status.lmac_valid = table[0];
        card.status.lmac_error_id = table[1];
        card.status.lmac_log_pc = table[20];
        card.status.error_log_read = true;
    }
    if (card.fw.umac_error_table >= IWX_SRAM_MIN_BASE &&
        sram_read(card.fw.umac_error_table, table, 2)) {
        card.status.umac_error_id = table[1];
        card.status.error_log_read = true;
    }
}
static bool service_interrupts(void) {
    u32 cause = reg_read(IWX_CSR_INT);
    card.status.csr_interrupt = cause;
    card.status.csr_gp = reg_read(IWX_CSR_GP_CNTRL);
    if (cause == ~0u || (cause & 0xfffffff0u) == 0xa5a5a5a0u ||
        (cause & (IWX_INT_SW_ERR | IWX_INT_HW_ERR))) {
        // Latch the flow-handler status next to the cause: on real
        // hardware this names the DMA engine that raised HW_ERR.
        card.status.csr_fh_status = reg_read(IWX_CSR_FH_INT_STATUS);
        if (cause != ~0u && (cause & (IWX_INT_SW_ERR | IWX_INT_HW_ERR))) capture_error_log();
        return false;
    }
    u32 msix_hw = reg_read(IWX_CSR_MSIX_HW_INT_CAUSES);
    if (msix_hw != ~0u && msix_hw) reg_write(IWX_CSR_MSIX_HW_INT_CAUSES, msix_hw);
    bool alive_seen = (cause & IWX_INT_ALIVE) || (msix_hw != ~0u && (msix_hw & 1u)) ||
                      (card.rx_status && (*card.rx_status & 0xfff));
    if (alive_seen && !card.rx_started) {
        // Buffers were described in transport_alloc; just open the queue.
        reg_write(IWX_CSR_RX_WPTR, 8);
        card.rx_started = true;
    }
    if (cause) reg_write(IWX_CSR_INT, cause);
    return true;
}
static bool wait_alive(void) {
    u64 start = time_counter_ns();
    for (;;) {
        if (!service_interrupts()) return false;
        const volatile u8* packet;
        u32 total = rx_poll(&packet);
        if (total >= 8 && packet[4] == IWX_NOTIF_ALIVE && packet[5] == 0) {
            u32 expected = card.fw.alive_version >= 6 ? 144 :
                           card.fw.alive_version == 5 ? 128 : 116;
            if (total != expected + 8 || packet[8] != 0xfe || packet[9] != 0xca)
                return false;
            card.alive = true;
            card.status.firmware_alive = true;
            return true;
        }
        if (time_counter_ns() - start >= IWX_ALIVE_NS) return false;
        __asm__ volatile("pause" ::: "memory");
    }
}
static bool init_failure(i32 error) {
    card.status.error = error;
    card.online = false;
    // Keep every DMA allocation quarantined; stop DMA before returning.
    if (card.regs) {
        card.status.csr_gp = reg_read(IWX_CSR_GP_CNTRL);
        card.status.csr_interrupt = reg_read(IWX_CSR_INT);
        reg_write(IWX_CSR_INT_MASK, 0);
    }
    if (card.status.present)
        command_write(&card.pci, (card.pci_command | 0x400u) & ~4u);
    return false;
}
bool iwx200_init(void) {
    u64 flags = irq_save();
    if (card.attempted) { irq_restore(flags); return card.online; }
    if (!card.firmware_ok) {
        card.status.stage = IWX200_FIRMWARE_MISSING;
        card.status.error = -2;
        irq_restore(flags);
        return false;
    }
    card.attempted = true;
    irq_restore(flags);
    card.pci = pci_scan_for_device(PCI_VENDOR_INTEL, IWX_PCI_AX200);
    pci_device_t* dev = &card.pci;
    if (dev->vendor_id != PCI_VENDOR_INTEL || dev->device_id != IWX_PCI_AX200) {
        card.status.stage = IWX200_ABSENT;
        return init_failure(-19);
    }
    card.status.present = true;
    card.pci_command = (u16)pci_read_config(dev->bus, dev->device, dev->function, 4);
    card.status.stage = IWX200_PCI_BAR;
    u32 low = pci_read_config(dev->bus, dev->device, dev->function, 0x10);
    u32 type = (low >> 1) & 3;
    if ((low & 1) || (type != 0 && type != 2)) return init_failure(-22);
    u64 bar = low & ~15u;
    if (type == 2) bar |= (u64)pci_read_config(dev->bus, dev->device, dev->function, 0x14) << 32;
    if (!bar || (bar & (MMIO_SIZE - 1)) || bar > ~0ULL - MMIO_SIZE) return init_failure(-22);
    command_write(dev, (card.pci_command | 0x002u) & ~0x405u);
    retry_timeout_clear(dev);
    bridges_enable(dev->bus);
    card.regs = (volatile u8*)MMIO_BASE;
    for (u32 off = 0; off < MMIO_SIZE; off += 4096)
        if (!vmm_map(MMIO_BASE + off, bar + off, 0x1b)) {
            card.regs = NULL;
            return init_failure(-12);
        }
    reg_write(IWX_CSR_INT_MASK, 0);
    card.status.stage = IWX200_PREPARE;
    if (!prepare_card()) return init_failure(-110);
    card.status.stage = IWX200_PERSISTENCE;
    if (!clear_persistence()) return init_failure(-1);
    card.status.stage = IWX200_RESET;
    reg_set(IWX_CSR_RESET, IWX_RESET_SW_RESET);
    u64 start = time_counter_ns();
    // Upstream reset delay is 5000 microseconds, not 5000 nanoseconds.
    while (time_counter_ns() - start < 5000000ULL) __asm__ volatile("pause" ::: "memory");
    card.status.stage = IWX200_CLOCK;
    if (!apm_init()) return init_failure(-110);
    // 22000-family parts default to MSI-X cause routing; without MSI-X we
    // must select MSI/legacy mode or causes never reach CSR_INT.
    if (!nic_lock()) return init_failure(-110);
    prph_write(IWX_UREG_CHICK, IWX_UREG_CHICK_MSI_ENABLE);
    reg_write(IWX_CSR_GP_CNTRL, reg_read(IWX_CSR_GP_CNTRL) & ~IWX_GP_MAC_ACCESS_REQ);
    u32 hw = reg_read(IWX_CSR_HW_REV), phy = card.fw.phy_config;
    u32 cfg = reg_read(IWX_CSR_HW_IF_CONFIG);
    cfg &= ~0x0000ff0fu;
    cfg |= (hw & 3) << 2; // CSR raw silicon step to software revision encoding
    cfg |= (phy & 3) << 10;
    cfg |= ((phy >> 2) & 3) << 14;
    cfg |= ((phy >> 4) & 3) << 12;
    reg_write(IWX_CSR_HW_IF_CONFIG, cfg);
    *(volatile u8*)(card.regs + IWX_CSR_INT_COALESCE) = 0x40;
    reg_set(IWX_CSR_MAC_SHADOW_CTRL, 0x800fffffu);
    card.status.stage = IWX200_MAC;
    if (!read_mac()) return init_failure(-5);
    card.status.stage = IWX200_RFKILL;
    // Sample the hardware RF-kill switch for diagnostics, but do not fail:
    // the bit also reads set while the device is still coming out of reset
    // on some platforms, and failing here strands the device with no path
    // to retry once the switch state settles.
    card.status.rfkill_clear = (reg_read(IWX_CSR_GP_CNTRL) & IWX_GP_HW_RFKILL_SW) != 0;
    reg_write(0x05c, 2u | 4u); // clear software RF-kill and command-blocked bits
    reg_write(IWX_CSR_INT, ~0u);
    reg_write(IWX_CSR_FH_INT_STATUS, ~0u);
    reg_write(IWX_CSR_INT_MASK, IWX_INT_ALIVE | IWX_INT_RX | IWX_INT_SW_ERR | IWX_INT_HW_ERR);
    card.status.stage = IWX200_DMA;
    if (!load_firmware()) return init_failure(-12);
    card.status.stage = IWX200_ALIVE;
    if (!wait_alive()) return init_failure(-110);
    card.online = true;
    card.status.error = 0;
    return true;
}
void iwx200_status(iwx200_status_t* out) {
    if (!out) return;
    *out = card.status;
    out->firmware_staged = card.firmware_ok;
    out->firmware_api = card.fw.api;
}
bool iwx200_online(void) { return card.online && card.alive; }
void iwx200_mac(u8 out[6]) { if (out) copy(out, card.mac, 6); }
// Stage a firmware image for the next init attempt. Copies nothing; the
// caller keeps the image alive until init completes. False when the card
// was already probed or the image is malformed.
bool iwx200_stage_fw(const void* image, u64 size) {
    if (card.attempted) return false;
    u32 sum = 2166136261u;
    for (u64 i = 0; i < size; i++) sum = (sum ^ ((const u8*)image)[i]) * 16777619u;
    card.status.firmware_sum = sum; card.status.firmware_size = (u32)size;
    if (!iwx200_parse_fw(image, size, &card.fw)) {
        card.status.stage = IWX200_FIRMWARE_INVALID; card.status.error = -22; return false;
    }
    card.firmware_ok = true;
    return true;
}
#define IWX_FW_PATH "/lib/firmware/iwlwifi-cc-a0-71.ucode"
// The blob is 1.3 MiB; bound the read so a corrupt length cannot exhaust
// the heap. The buffer stays permanently allocated for the staged image.
#define IWX_FW_MAX_READ (2u * 1024u * 1024u)
void iwx200_late_init(void) {
    if (card.attempted) return;
    pci_device_t device = pci_scan_for_device(PCI_VENDOR_INTEL, IWX_PCI_AX200);
    if (device.vendor_id != PCI_VENDOR_INTEL || device.device_id != IWX_PCI_AX200) {
        card.status.stage = IWX200_ABSENT; card.status.error = -19;
        card.attempted = true;
        return;
    }
    card.status.present = true;
    file_t* file;
    if (file_open(IWX_FW_PATH, O_RDONLY, &file) < 0) {
        card.status.stage = IWX200_FIRMWARE_MISSING; card.status.error = -2;
        klog(LOG_WARN, "iwx200: AX200 present, firmware missing: %s\n", IWX_FW_PATH);
        return;
    }
    u8* image = kmalloc(IWX_FW_MAX_READ);
    if (!image) {
        card.status.stage = IWX200_FIRMWARE_MISSING; card.status.error = -12;
        klog(LOG_WARN, "iwx200: AX200 firmware buffer allocation failed\n");
        file_unref(file); return;
    }
    u64 used = 0;
    for (;;) {
        i64 n = file_read(file, image + used, IWX_FW_MAX_READ - used);
        if (n < 0) {
            card.status.stage = IWX200_FIRMWARE_INVALID; card.status.error = (i32)n;
            klog(LOG_WARN, "iwx200: AX200 firmware read failed: %d\n", (i32)n);
            kfree(image); file_unref(file); return;
        }
        if (!n) break;
        used += (u64)n;
        if (used == IWX_FW_MAX_READ) break;
    }
    file_unref(file);
    if (!used || !iwx200_stage_fw(image, used)) {
        card.status.stage = IWX200_FIRMWARE_INVALID; card.status.error = -22;
        klog(LOG_WARN, "iwx200: AX200 invalid firmware image: %s\n", IWX_FW_PATH);
        kfree(image); return;
    }
    // The staged image borrows this buffer; it is never freed.
    if (!iwx200_init()) {
        klog(LOG_WARN, "iwx200: AX200 bootstrap failed stage=%u error=%d int=%08x gp=%08x\n",
             (u32)card.status.stage, card.status.error, card.status.csr_interrupt, card.status.csr_gp);
        return;
    }
    if (iwx200_run_init()) { klog(LOG_WARN, "iwx200: AX200 NVM init failed: %d\n", card.status.error); return; }
    klog(LOG_OKAY, "iwx200: AX200 firmware alive, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
         card.mac[0], card.mac[1], card.mac[2], card.mac[3], card.mac[4], card.mac[5]);
}
// --- Command transport ---
// A TFD entry: transfer descriptor for one command. Two transfer buffers
// at most: the first20B plus the remaining command bytes.
typedef struct __attribute__((packed)) {
    u16 count;
    struct __attribute__((packed)) { u16 length; u64 address; } tb[25];
    u32 pad;
} tfd_t;
_Static_assert(sizeof(tfd_t) == 256, "Intel long TFD ABI");
_Static_assert(__builtin_offsetof(tfd_t, tb[0].length) == 2, "TFD TB length");
_Static_assert(__builtin_offsetof(tfd_t, tb[0].address) == 4, "TFD TB address");
_Static_assert(sizeof(((tfd_t*)0)->tb[0]) == 10, "TFD TB stride");
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
    u64 tx_phys, free_phys, status_phys, used_phys;
    u8* tx = dma_contig(&tx_phys, IWX_TX_RING_COUNT * 256);
    u8* free_ring = dma_contig(&free_phys, IWX_RX_RING_COUNT * 8);
    u8* status = dma_contig(&status_phys, 16);
    u8* used_ring = dma_contig(&used_phys, IWX_RX_RING_COUNT * 4);
    if (!tx || !free_ring || !status || !used_ring) return false;
    card.rx_used_phys = used_phys;
    card.tx_ring = (volatile u8*)tx;
    card.tx_phys = tx_phys;
    for (u32 i = 0; i < IWX_TX_RING_COUNT; i++) {
        card.cmd_buffers[i] = dma_page(&card.cmd_addresses[i]);
        if (!card.cmd_buffers[i]) return false;
    }
    card.rx_free_phys = free_phys;
    card.rx_status_phys = status_phys;
    card.rx_free = (u64*)free_ring;
    card.rx_status = (volatile u16*)status;
    // Describe every receive buffer before the firmware runs: the first
    // thing it does after self-load is DMA its ALIVE notification, and it
    // has no other way to learn where to write it.
    for (u32 i = 0; i < IWX_RX_RING_COUNT; i++) {
        u64 phys;
        u8* buffer = dma_contig(&phys, IWX_RX_BUFFER_SIZE);
        if (!buffer) return false;
        card.rx_buffers[i] = buffer;
        card.rx_free[i] = phys | (i & 0xfff);
    }
    fence();
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
    if (!card.rx_status || !card.rx_started) return 0;
    // iwx_notif_intr masks closed_rb_num to the configured cyclic ring:
    // hardware status exposes12bits, our AX200 ring uses9bits (512 slots).
    u16 closed = *card.rx_status & (IWX_RX_RING_COUNT - 1);
    if (card.rx_next == closed) return 0;
    u32 index = card.rx_next, offset = card.rx_offset;
    const volatile u8* buffer = card.rx_buffers[index];
    u32 wire = get32(buffer + offset);
    u32 size = wire & 0x3fff;
    // Wire size includes the four-byte command header, excludes length word.
    if (wire == 0x55550000u || size < 4 || size > IWX_RX_BUFFER_SIZE - offset - 4) {
        card.rx_offset = 0;
        zero(card.rx_buffers[index], IWX_RX_BUFFER_SIZE);
        card.rx_next = (index + 1) % IWX_RX_RING_COUNT;
        rx_replenish(index);
        reg_write(IWX_CSR_RX_WPTR, ((card.rx_next + IWX_RX_RING_COUNT - 1) % IWX_RX_RING_COUNT) & ~7u);
        return 0;
    }
    u32 total = size + 4;
    for (u32 i = 0; i < total; i++) card.rx_packet[i] = buffer[offset + i];
    *payload_out = card.rx_packet;
    u32 next = offset + ((total + 63) & ~63u);
    u32 next_wire = next + 8 <= IWX_RX_BUFFER_SIZE ? get32(buffer + next) : 0;
    if (next_wire && next_wire != 0x55550000u && (next_wire & 0x3fff) >= 4) {
        card.rx_offset = next;
    } else {
        card.rx_offset = 0;
        zero(card.rx_buffers[index], IWX_RX_BUFFER_SIZE);
        card.rx_next = (index + 1) % IWX_RX_RING_COUNT;
        rx_replenish(index);
        reg_write(IWX_CSR_RX_WPTR, ((card.rx_next + IWX_RX_RING_COUNT - 1) % IWX_RX_RING_COUNT) & ~7u);
    }
    return total;
}
// Send a synchronous command and wait for its response. Polls the RX ring
// with a TSC deadline; never blocks. Matched by slot index.
i32 iwx200_command(u32 group, u32 opcode, u32 version,
                   const void* payload, u32 payload_length,
                   void* response, u32 capacity) {
    if (!card.online) return -22; // EINVAL
    if ((!payload && payload_length) || payload_length > IWX_CMD_MAX_PAYLOAD) return -90; // EMSGSIZE
    if (!response && capacity) return -22;
    u64 flags = irq_save();
    if (card.command_busy) { irq_restore(flags); return -16; }
    card.command_busy = true;
    irq_restore(flags);
    if (!group) group = IWX_GRP_LONG;
    u32 slot = card.tx_next;
    u8 index = card.tx_index++;
    volatile u8* cmd = card.cmd_buffers[slot];
    cmd[0] = (u8)opcode; cmd[1] = (u8)group; cmd[2] = index; cmd[3] = 0;
    cmd_put16(cmd + 4, (u16)payload_length); cmd[6] = 0; cmd[7] = (u8)version;
    for (u32 i = 0; i < payload_length; i++) cmd[8 + i] = ((const u8*)payload)[i];    // TFD starts with the writable20-byte scratch transfer buffer.
    volatile u8* tfd = card.tx_ring + (u64)slot * 256;
    u64 cmd_phys = card.cmd_addresses[slot];
    cmd_put16(tfd, 1); // one TB
    for (u32 i = 2; i < 256; i++) tfd[i] = 0;
    u32 first = 8 + payload_length > 20 ? 20 : 8 + payload_length;
    cmd_put16(tfd + 2, (u16)first);
    cmd_put32(tfd + 4, (u32)cmd_phys);
    cmd_put32(tfd + 8, (u32)(cmd_phys >> 32));
    if (8 + payload_length > first) {
        cmd_put16(tfd, 2);
        cmd_put16(tfd + 12, (u16)(8 + payload_length - first));
        cmd_put32(tfd + 14, (u32)(cmd_phys + first));
        cmd_put32(tfd + 18, (u32)((cmd_phys + first) >> 32));
    }
    fence();
    card.tx_next = (card.tx_next + 1) % IWX_TX_RING_COUNT;
    reg_write(IWX_HBUS_WRPTR, (IWX_CMD_QUEUE << 16) | card.tx_next);
    fence();
    // Wait for the response with our slot index.
    u64 start = time_counter_ns();
    i32 result = -110; // ETIMEDOUT
    for (;;) {
        if (!service_interrupts()) { result = -5; break; }
        const volatile u8* buffer;
        u32 length = rx_poll(&buffer);
        if (length >= 8) {
            if (data_completion(buffer, length)) continue;
            u8 code = buffer[4], rflags = buffer[5], rindex = buffer[6], queue = buffer[7];
            u32 payload_length = length - 8;
            // Unsolicited notifications (high queue bit) are recorded for
            // the MLME layer; solicited responses match by slot index.
            // RX MPDUs (0xc1) carry data/management frames: stash the whole
            // notification so mgmt_rx can retrieve it after the command.
            if (queue & 0x80) {
                card.notif_code = code;
                card.notif_group = rflags;
                card.notif_length = payload_length > sizeof(card.notif_payload) ?
                    sizeof(card.notif_payload) : payload_length;
                for (u32 i = 0; i < card.notif_length; i++)
                    card.notif_payload[i] = buffer[8 + i];
                card.notif_pending = true;
                if (card.notification) card.notification(rflags, code, card.rx_packet + 8, payload_length);
                if (code == IWX_CMD_INIT_COMPLETE && rflags == 0) {
                    card.init_complete = true;
                    // Keep polling for the solicited command acknowledgement.
                }
                continue;
            }
            if (rindex == index && queue == IWX_CMD_QUEUE &&
                ((code == opcode && (rflags & ~IWX_CMD_FAILED) == group) || (code == 2 && rflags == 0))) {
                if ((rflags & IWX_CMD_FAILED) || (code == 2 && rflags == 0)) result = -71; // EPROTO: rejected, card still alive
                else if (!response && !capacity) result = 0;
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
    flags = irq_save();
    card.command_busy = false;
    irq_restore(flags);
    if (result < 0) {
        card.status.cmd_fail_id = (group << 8) | opcode;
        card.status.cmd_fail_result = result;
        card.status.error = result;
        if (result == -110 || result == -5) init_failure(result);
    }
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
    if (card.status.nvm_ready) return 0;
    u8 payload[8];
    u8 response[512];
    card.status.stage = IWX200_NVM;
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
    bool complete = card.init_complete;
    card.notif_pending = false;
    for (u32 attempt = 0; !complete && attempt < 2; attempt++) {
        u64 start = time_counter_ns();
        while (time_counter_ns() - start < IWX_CMD_NS) {
            const volatile u8* buffer;
            if (!service_interrupts()) { init_failure(-5); return -5; }
            if (rx_poll(&buffer) >= 8 && (buffer[7] & 0x80) && buffer[4] == IWX_CMD_INIT_COMPLETE && buffer[5] == 0)
                { complete = true; card.init_complete = true; break; }
            __asm__ volatile("pause" ::: "memory");
        }
    }
    if (!complete) { init_failure(-110); return -110; }
    put32le(payload, 0);
    i32 n = iwx200_command(IWX_GRP_REGULATORY, IWX_REG_NVM_GET_INFO, 0, payload, 4, response, sizeof(response));
    u32 expected = card.fw.nvm_version >= 4 ? 468 : 128;
    if (n != (i32)expected) { init_failure(-5); return -5; }
    // NVM general section: flags(4) version(2) board(1) addrs(1).
    u8 addrs = response[7];
    if (!addrs) { init_failure(-5); return -5; }
    card.nvm.tx_chains = get32(response + 12);
    card.nvm.rx_chains = get32(response + 16);
    card.nvm.phy_config = card.fw.phy_config;
    card.nvm.lar_enabled = get32(response + 20);
    card.nvm.channel_count = card.fw.nvm_version >= 4 ? get32(response + 24) : 51;
    if (card.nvm.channel_count > 110) { init_failure(-5); return -5; }
    for (u32 i = 0; i < card.nvm.channel_count; i++)
        card.nvm.channel_profiles[i] = card.fw.nvm_version >= 4 ? get32(response + 28 + i * 4) :
            (u32)response[24 + i * 2] | ((u32)response[25 + i * 2] << 8);
    // The factory MAC comes from the strap registers (already validated
    // during probe); NVM confirms an address exists for this card.
    copy(nvm_mac, card.mac, 6);
    card.status.nvm_ready = true;
    card.status.stage = IWX200_READY;
    return 0;
}
void iwx200_nvm_mac(u8 out[6]) { if (out) copy(out, nvm_mac, 6); }
bool iwx200_has_capability(u32 bit) {
    return bit < 256 && (card.fw.capabilities[bit / 32] >> (bit % 32) & 1);
}
i32 iwx200_cmd_version(u8 group, u8 opcode) {
    if (!group) group = IWX_GRP_LONG;
    for (u32 i = 0; i < card.fw.command_version_count; i++) {
        const u8* row = card.fw.command_versions[i];
        if (row[0] == opcode && row[1] == group) return row[2] == 99 ? -1 : row[2];
    }
    return -1;
}
i32 iwx200_notif_version(u8 group, u8 opcode) {
    for (u32 i = 0; i < card.fw.command_version_count; i++) {
        const u8* row = card.fw.command_versions[i];
        if (row[0] == opcode && row[1] == group) return row[3] == 99 ? -1 : row[3];
    }
    return -1;
}
void iwx200_set_notification_handler(iwx200_notification_fn callback) { card.notification = callback; }
void iwx200_poll(void) {
    if (!card.online) return;
    u64 flags = irq_save();
    if (card.command_busy) { irq_restore(flags); return; }
    card.command_busy = true;
    irq_restore(flags);
    if (!service_interrupts()) { init_failure(-5); }
    else for (u32 i = 0; i < IWX_RX_RING_COUNT; i++) {
        const volatile u8* packet;
        u32 total = rx_poll(&packet);
        if (!total) break;
        if (data_completion(packet, total)) continue;
        if ((packet[7] & 0x80) && card.notification)
            card.notification(packet[5], packet[4], card.rx_packet + 8, total - 8);
    }
    flags = irq_save(); card.command_busy = false; irq_restore(flags);
}
bool iwx200_get_nvm(iwx200_nvm_t* out) {
    if (!out || !card.status.nvm_ready) return false;
    *out = card.nvm;
    return true;
}

i32 iwx200_configure_radio(void) {
    if (!card.online || !card.status.nvm_ready) return -19;
    if (card.radio_ready) return 0;
    // The currently bundled, audited discrete firmware is cc-a0 API71.
    if (card.fw.api != 71) return -95;
    u8 cmd[8] = {0}, response[16];
    put32le(cmd, card.nvm.tx_chains & ((card.fw.phy_config >> 16) & 15));
    if (!get32(cmd)) return -5;
    i32 n = iwx200_command(0, 0x98, 0, cmd, 4, response, sizeof(response));
    if (n < 0) return n;
    // AX200 has no SISO diversity: upstream does not send PHY_CFG here.
    put32le(cmd, 3); put32le(cmd + 4, 0); // BT_COEX_WIFI, no modules
    n = iwx200_command(0, 0x9b, 0, cmd, 8, response, sizeof(response));
    if (n < 0) return n;
    put32le(cmd, 1); // SOC discrete, crystal latency0 for cc-a0
    n = iwx200_command(2, 1, 0, cmd, 8, response, sizeof(response));
    if (n < 0) return n;
    // OpenBSD gates DQA_ENABLE on capability12. The bundled API71 does
    // not advertise it; gen2 queue configuration works without this command.
    if (card.fw.capabilities[0] & (1u << 12)) {
        put32le(cmd, 0);
        n = iwx200_command(5, 0, 0, cmd, 4, response, sizeof(response));
        if (n < 0) return n;
    }
    card.radio_ready = true;
    return 0;
}
static i32 data_slot(u8 qid) {
    for (u32 i = 0; i < 4; i++) if (card.data[i].active && card.data[i].qid == qid) return i;
    return -1;
}
i32 iwx200_data_queue_enable(u8 station_id, u8 tid) {
    if (!card.online || !card.radio_ready) return -19;
    if (station_id >= 32 || tid > 15) return -22;
    if (iwx200_cmd_version(5, 0x17) >= 0 || iwx200_cmd_version(1, 0x1d) != 2) return -95;
    if (iwx200_cmd_version(1, 0x1c) != 9 || iwx200_notif_version(1, 0x1c) != 7) return -95;
    i32 slot = -1;
    for (u32 i = 0; i < 4; i++) {
        if (card.data[i].active && card.data[i].station == station_id && card.data[i].tid == tid)
            return card.data[i].qid;
        if (!card.data[i].active && slot < 0) slot = i;
    }
    if (slot < 0) return -28;
    if (!card.data[slot].allocated) {
        card.data[slot].ring = dma_contig(&card.data[slot].ring_phys, IWX_TX_RING_COUNT * 256);
        card.data[slot].byte_counts = dma_page(&card.data[slot].byte_counts_phys);
        if (!card.data[slot].ring || !card.data[slot].byte_counts) return -12;
        card.data[slot].allocated = true;
    }
    zero(card.data[slot].ring, IWX_TX_RING_COUNT * 256);
    zero(card.data[slot].byte_counts, 4096);
    u8 cmd[24] = {0}, response[8];
    cmd[0] = station_id; cmd[1] = tid; cmd_put16(cmd + 2, 1);
    put32le(cmd + 4, 5); //256 entries, exponent minus3
    copy(cmd + 8, &card.data[slot].byte_counts_phys, 8);
    copy(cmd + 16, &card.data[slot].ring_phys, 8);
    i32 n = iwx200_command(0, 0x1d, 0, cmd, sizeof(cmd), response, sizeof(response));
    if (n < 0) return n;
    card.status.queue_response_length = (u32)n;
    card.status.queue_response = (u32)response[0] | (u32)response[1] << 8 | (u32)response[2] << 16 | (u32)response[3] << 24;
    card.status.queue_response_hi = (u32)response[4] | (u32)response[5] << 8 | (u32)response[6] << 16 | (u32)response[7] << 24;
    // queue_reject: 1 length, 2 failure flags, 3 qid range, 4 qid already in use
    card.status.queue_reject = 0;
    if (n != 8) { card.status.queue_reject = 1; return -5; }
    // response[2..3] is a firmware-internal flags word. Neither OpenBSD
    // (iwx_enable_txq) nor iwlwifi (iwl_pcie_txq_alloc_response) treats it
    // as an error; only the queue number and write pointer matter.
    u32 qid = (u32)response[0] | ((u32)response[1] << 8);
    // Firmware reports a free-running write pointer; iwlwifi masks it to the
    // ring size (iwl_pcie_txq_alloc_response), so do the same.
    u32 pointer = ((u32)response[4] | ((u32)response[5] << 8)) & (IWX_TX_RING_COUNT - 1);
    if (!qid || qid >= 32) { card.status.queue_reject = 3; return -5; }
    if (data_slot(qid) >= 0) { card.status.queue_reject = 4; return -5; }
    card.data[slot].active = true; card.data[slot].qid = qid;
    card.data[slot].station = station_id; card.data[slot].tid = tid;
    card.data[slot].head = card.data[slot].tail = pointer;
    card.data[slot].pending = 0;
    return qid;
}
i32 iwx200_data_queue_disable(u8 qid) {
    i32 slot = data_slot(qid);
    if (slot < 0) return -22;
    u8 cmd[24] = {0}, response[8];
    cmd[0] = card.data[slot].station; cmd[1] = card.data[slot].tid;
    i32 n = iwx200_command(0, 0x1d, 0, cmd, sizeof(cmd), response, sizeof(response));
    if (n < 0) return n;
    if (n != 8) return -5;
    card.data[slot].active = false;
    card.data[slot].pending = 0;
    return 0;
}
static void data_tb(volatile u8* descriptor, u32 tb, u64 address, u16 length) {
    u32 offset = 2 + tb * 10;
    cmd_put16(descriptor + offset, length);
    cmd_put32(descriptor + offset + 2, (u32)address);
    cmd_put32(descriptor + offset + 6, (u32)(address >> 32));
}
bool iwx200_data_tx(u8 qid, const u8 prefix[20], const void* frame, u32 length) {
    i32 which = data_slot(qid);
    if (!card.online || which < 0 || !prefix || !frame || length < 24 || length > 2304) return false;
    const u8* bytes = frame;
    u32 header = 24 + (((bytes[0] & 0x8c) == 0x88) ? 2 : 0);
    if ((bytes[1] & 3) == 3 || length < header) return false;
    if (((u32)prefix[0] | ((u32)prefix[1] << 8)) != length) return false;
    u32 slot = card.data[which].head;
    if (card.data[which].pending >= IWX_TX_RING_COUNT - 1) return false;
    if (!card.data[which].buffers[slot]) {
        card.data[which].buffers[slot] = dma_page(&card.data[which].addresses[slot]);
        if (!card.data[which].buffers[slot]) return false;
    }
    u8* buffer = card.data[which].buffers[slot];
    u64 physical = card.data[which].addresses[slot];
    u32 pad = (4 - (header & 3)) & 3;
    u32 first_size = 4 + 20 + header + pad;
    zero(buffer, 4096);
    buffer[0] = 0x1c; buffer[2] = slot; buffer[3] = qid; // narrow TX header
    copy(buffer + 4, prefix, 20);
    u16 assist = (u16)(prefix[2] | ((u16)prefix[3] << 8));
    assist &= ~((31u << 8) | (1u << 13));
    assist |= (header / 2) << 8;
    if (pad) assist |= 1u << 13;
    cmd_put16(buffer + 6, assist);
    copy(buffer + 24, frame, header);
    copy(buffer + first_size, bytes + header, length - header);
    volatile u8* descriptor = card.data[which].ring + slot * 256;
    zero((void*)descriptor, 256);
    cmd_put16(descriptor, length > header ? 3 : 2);
    data_tb(descriptor, 0, physical, 20);
    data_tb(descriptor, 1, physical + 20, first_size - 20);
    if (length > header) data_tb(descriptor, 2, physical + first_size, length - header);
    // Gen2 scheduler length is in dwords; three TBs fit one64B TFD chunk.
    card.data[which].byte_counts[slot] = (length + 3) / 4;
    fence();
    card.data[which].head = (slot + 1) % IWX_TX_RING_COUNT;
    card.data[which].pending++;
    reg_write(IWX_HBUS_WRPTR, ((u32)qid << 16) | card.data[which].head);
    return true;
}
static bool data_completion(const volatile u8* packet, u32 total) {
    u8 qid = packet[7];
    i32 which = data_slot(qid);
    if (which < 0 || packet[4] != 0x1c || packet[5] != 0) return false;
    // API71 TX_RSPv7 uses the current44B fixed response plus trailing SSN.
    // Aggregation is disabled: only frame_count1 is valid for these queues.
    if (total < 8 + 48 || packet[8] != 1) return true;
    u32 ssn = get32(packet + 8 + 44) & 0xfff;
    if (ssn >= IWX_TX_RING_COUNT) return true;
    u32 completed = (ssn + IWX_TX_RING_COUNT - card.data[which].tail) % IWX_TX_RING_COUNT;
    if (completed > card.data[which].pending) return true;
    card.data[which].pending -= completed;
    card.data[which].tail = ssn;
    return true;
}
