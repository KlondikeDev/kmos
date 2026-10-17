// This file has been generated in whole or in part by a Large Language Model
// gcc -Ibuildutils/tests/iwx200_stubs -Iinclude -Iinclude/klibc -Wall -Wextra
//     -Werror -fsanitize=address,undefined buildutils/tests/iwx200_protocol.c
//     kernel/wifi.c kernel/wifi_wpa.c kernel/wifi_sha1.c kernel/wifi_aes.c
//     -o /tmp/iwx200_protocol
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../drivers/iwx200.c"
#include "../../kernel/wifi.c"
#include "../../kernel/wifi_wpa.c"
#include "../../kernel/wifi_sha1.c"
#include "../../kernel/wifi_aes.c"
// Deterministic entropy for the handshake test.
i64 random_read(void* buffer, u64 length) {
    for (u64 i = 0; i < length; i++) ((u8*)buffer)[i] = (u8)(0x5a + i);
    return (i64)length;
}

static u32 registers[0x20000 / 4];
static u64 clock_ns;
static u32 pci_address;
static u16 pci_command;
static bool present;
static bool stuck_clock;
void outl(u16 port, u32 value) { assert(port == PCI_CONFIG_ADDRESS); pci_address = value; }
void outw(u16 port, u16 value) { assert(port == PCI_CONFIG_DATA); pci_command = value; }
pci_device_t pci_scan_for_device(u16 v, u16 d) {
    (void)v; (void)d;
    return present ? (pci_device_t){ .vendor_id = v, .device_id = d } : (pci_device_t){0};
}
u32 pci_read_config(u8 b, u8 d, u8 f, u8 off) { (void)b; (void)d; (void)f; return off == 0x10 ? 0xf0000000u : 0; }
// Bump-pointer arena: consecutive allocations are physically contiguous,
// matching the kernel's permanent DMA reservation semantics.
static u8 arena[12 * 1024 * 1024] __attribute__((aligned(4096)));
static u64 arena_used;
void* pmm_alloc(void) {
    if (arena_used + 4096 > sizeof(arena)) return NULL;
    void* p = arena + arena_used; arena_used += 4096;
    memset(p, 0, 4096);
    return p;
}
void pmm_free(uint64_t addr) { (void)addr; /* arena pages stay reserved, like the kernel */ }
uint64_t vmm_hhdm(void) { return 0; }
bool vmm_map(uint64_t v, uint64_t p, uint64_t f) {
    assert(v >= MMIO_BASE && v < MMIO_BASE + MMIO_SIZE);
    (void)p; assert(f == 0x1b);
    card.regs = (volatile u8*)registers; return true;
}
// Loader/file stubs: late_init is not under test, only linked.
typedef struct file file_t;
i32 file_open(const char* p, u32 f, file_t** o) { (void)p; (void)f; (void)o; return -2; }
void file_unref(file_t* f) { (void)f; }
i64 file_read(file_t* f, void* b, u64 n) { (void)f; (void)b; (void)n; return -5; }
void* kmalloc(uint64_t n) { (void)n; return NULL; }
void kfree(void* p) { (void)p; }
kernel_logs_t log_messages[4];
void klog(kernel_logs_t* l, const char* f, ...) { (void)l; (void)f; }
u64 time_counter_ns(void) {    clock_ns += 1000000;
    if (stuck_clock) registers[IWX_CSR_HW_REV / 4] = ~0u;
    // The card raises its clock-ready bit once the host sets INIT_DONE.
    if (registers[IWX_CSR_GP_CNTRL / 4] & IWX_GP_INIT_DONE)
        registers[IWX_CSR_GP_CNTRL / 4] |= IWX_GP_MAC_CLOCK_READY;
    // Emulated firmware and AP: when the driver kicks the TX write
    // pointer, answer the newest command slot. MLME commands get empty
    // replies; TX commands (0x1c) are parsed as 802.11 frames and answered
    // as RX MPDU notifications (24-byte descriptor + frame).
    {
        static u32 last_kick = 0;
        static bool init_notified = false;
        u32 kick = registers[IWX_HBUS_WRPTR / 4];
        if (kick != last_kick && card.cmd_data && card.rx_buffers[0]) {
            last_kick = kick;
            u32 slot = (card.tx_next + IWX_TX_RING_COUNT - 1) % IWX_TX_RING_COUNT;
            volatile u8* cmd = card.cmd_data + (u64)slot * 324;
            u8 group = cmd[1], opcode = cmd[0];
            u32 rx = (u32)(*card.rx_status & 0x1ff);
            volatile u8* buffer = card.rx_buffers[rx % IWX_RX_RING_COUNT];
            if (group == IWX_GRP_LEGACY && opcode == 0x1c) {
                // TX frame: 8-byte TX_CMD prefix, then the 802.11 frame.
                u16 tx_length = (u16)(cmd[8] | ((u16)cmd[9] << 8));
                const volatile u8* frame = cmd + 16;
                u8 subtype = (u8)((frame[0] >> 4) & 15);
                u8 type = (u8)((frame[0] >> 2) & 3);
                u8 ap_reply[256]; u32 reply_length = 0;
                if (type == 0 && subtype == 0x0b) {
                    // Auth request -> open-system auth reply (seq 2).
                    for (u32 i = 0; i < 24; i++) ap_reply[i] = frame[i];
                    for (u32 i = 0; i < 6; i++) { u8 t = ap_reply[4+i]; ap_reply[4+i] = ap_reply[10+i]; ap_reply[10+i] = t; }
                    ap_reply[24] = 0; ap_reply[25] = 0; ap_reply[26] = 2; ap_reply[27] = 0;
                    ap_reply[28] = 0; ap_reply[29] = 0;
                    reply_length = 30;
                } else if (type == 0 && subtype == 0x00) {
                    // Assoc request -> assoc reply with AID 1.
                    for (u32 i = 0; i < 24; i++) ap_reply[i] = frame[i];
                    ap_reply[0] = (u8)((ap_reply[0] & 0x0f) | (0x01 << 4)); // assoc response subtype
                    for (u32 i = 0; i < 6; i++) { u8 t = ap_reply[4+i]; ap_reply[4+i] = ap_reply[10+i]; ap_reply[10+i] = t; }
                    ap_reply[24] = 0; ap_reply[25] = 0; ap_reply[26] = 0; ap_reply[27] = 0;
                    ap_reply[28] = 1; ap_reply[29] = 0xc0;
                    reply_length = 30;
                } else if (type == 2) {
                    // Data frame: echo it back from-DS (open-network loopback).
                    // addr1 = receiver (TX addr2), addr2 = AP (TX addr1),
                    // addr3 = original source (TX addr2, as the AP forwards).
                    ap_reply[0] = 0x08; ap_reply[1] = 0x02;
                    ap_reply[2] = ap_reply[3] = 0;
                    for (u32 i = 0; i < 6; i++) { ap_reply[4+i] = frame[10+i]; ap_reply[10+i] = frame[4+i]; ap_reply[16+i] = frame[10+i]; }
                    ap_reply[22] = ap_reply[23] = 0;
                    // TX length covers the 8-byte TX_CMD prefix plus the
                    // 802.11 frame; the payload starts past both.
                    u32 payload = tx_length > 32 ? tx_length - 32 : 0;
                    if (payload > sizeof(ap_reply) - 24) payload = 0;
                    for (u32 i = 0; i < payload; i++) ap_reply[24+i] = frame[24+i];
                    reply_length = 24 + payload;
                }
                (void)tx_length;
                if (reply_length) {
                    u32 total = 8 + 16 + reply_length;
                    buffer[0] = (u8)total; buffer[1] = (u8)(total >> 8);
                    buffer[2] = buffer[3] = 0;
                    buffer[4] = 0xc1; buffer[5] = 0; buffer[6] = 0; buffer[7] = 0x80;
                    for (u32 i = 0; i < 16; i++) buffer[8+i] = 0; // descriptor
                    for (u32 i = 0; i < reply_length; i++) buffer[24+i] = ap_reply[i];
                    *card.rx_status = (u16)(*card.rx_status + 1);
                    rx = (u32)(*card.rx_status & 0x1ff);
                    buffer = card.rx_buffers[rx % IWX_RX_RING_COUNT];
                }
                // Solicited TX confirmation for the command slot.
                buffer[0] = 8; buffer[1] = buffer[2] = buffer[3] = 0;
                buffer[4] = opcode; buffer[5] = 0; buffer[6] = cmd[2]; buffer[7] = 0;
                *card.rx_status = (u16)(*card.rx_status + 1);
                return clock_ns;
            }
            if (group == IWX_GRP_REGULATORY && opcode == IWX_REG_NVM_ACCESS_COMPLETE && !init_notified) {
                init_notified = true;
                // Unsolicited INIT_COMPLETE before the solicited reply.
                buffer[0] = 8; buffer[1] = buffer[2] = buffer[3] = 0;
                buffer[4] = IWX_CMD_INIT_COMPLETE; buffer[5] = 0; buffer[6] = 0; buffer[7] = 0x80;
                *card.rx_status = (u16)(*card.rx_status + 1);
                rx = (u32)(*card.rx_status & 0x1ff);
                buffer = card.rx_buffers[rx % IWX_RX_RING_COUNT];
            }
            if (group == IWX_GRP_REGULATORY && opcode == IWX_REG_NVM_GET_INFO) {
                buffer[0] = 16; buffer[1] = buffer[2] = buffer[3] = 0;
                buffer[4] = opcode; buffer[5] = 0; buffer[6] = cmd[2]; buffer[7] = 0;
                buffer[8] = buffer[9] = buffer[10] = buffer[11] = 0; // flags
                buffer[12] = 0x34; buffer[13] = 0x12; // version
                buffer[14] = 0x42; buffer[15] = 0x01; // board, one address
            } else {
                buffer[0] = 12; buffer[1] = buffer[2] = buffer[3] = 0;
                buffer[4] = opcode; buffer[5] = 0; buffer[6] = cmd[2]; buffer[7] = 0;
                buffer[8] = 0x78; buffer[9] = 0x56; buffer[10] = 0x34; buffer[11] = 0x12;
            }
            *card.rx_status = (u16)(*card.rx_status + 1);
        }
    }
    return clock_ns;
}
// Minimal valid TLV image: header plus one INIT section TLV with a 64-byte
// payload (device offset word + 60 bytes of code).
static u32 build_image(u8* image) {
    memset(image, 0, 256);
    image[4] = 0x49; image[5] = 0x57; image[6] = 0x4c; image[7] = 0x0a;
    image[72] = 77; // API version 77
    image[88] = IWX_TLV_SEC_RT; image[92] = 64;
    image[96] = 0x40; image[97] = 0x00; image[98] = 0x00; image[99] = 0x00;
    for (u32 i = 100; i < 160; i++) image[i] = (u8)i;
    return 160;
}
int main(void) {
    u8 image[256];
    u32 image_length = build_image(image);
    iwx200_fw_t fw;
    assert(!iwx200_parse_fw(NULL, image_length, &fw));
    assert(!iwx200_parse_fw(image, 10, &fw));
    image[4] = 0x00; assert(!iwx200_parse_fw(image, image_length, &fw)); // bad magic
    image[4] = 0x49;
    assert(iwx200_parse_fw(image, image_length, &fw));
    assert(fw.api == 77 && fw.regular_count == 1 && fw.regular[0].length == 60);
    image[92] = 200; assert(!iwx200_parse_fw(image, image_length, &fw)); // overrun
    image[92] = 64;
    assert(iwx200_parse_fw(image, image_length, &fw));
    // Second TLV: the CPU1/CPU2 separator (zero-length code section).
    image[160] = IWX_TLV_SEC_RT; image[164] = 8;
    image[168] = 0xcc; image[169] = 0xcc; image[170] = 0xff; image[171] = 0xff;
    image[172] = 0xbb; image[173] = 0xbb; image[174] = 0xaa; image[175] = 0xaa;
    assert(iwx200_parse_fw(image, image_length + 16, &fw) && fw.regular_count == 2);
    // Third TLV: a UMAC section after the separator.
    image[176] = IWX_TLV_SEC_RT; image[180] = 8;
    image[184] = 0x00; image[185] = 0x10; image[186] = 0x00; image[187] = 0x00;
    image[188] = 0xaa; image[189] = 0xbb; image[190] = 0xcc; image[191] = 0xdd;
    assert(iwx200_parse_fw(image, image_length + 32, &fw) && fw.regular_count == 3);
    assert(fw.regular[2].device_offset == 0x1000 && fw.regular[2].length == 4);
    // Init sequence against fake PCI/MMIO: absent card refuses.
    present = false;
    assert(!iwx200_init() && !iwx200_online());
    // Present card without firmware refuses after MAC validation.
    memset(&card, 0, sizeof(card)); present = true; clock_ns = 0;
    registers[IWX_CSR_HW_IF_CONFIG / 4] = IWX_HW_IF_NIC_READY;
    registers[IWX_CSR_MAC_CSR_BASE / 4 + 2] = 0x00112233u;
    registers[IWX_CSR_MAC_CSR_BASE / 4 + 3] = 0x00004455u;
    assert(!iwx200_init()); // no firmware staged
    // Staged firmware but no alive announcement times out.
    memset(&card, 0, sizeof(card));
    assert(iwx200_stage_fw(image, image_length + 32));
    assert(!iwx200_init());
    // Alive announcement completes init.
    memset(&card, 0, sizeof(card));
    assert(iwx200_stage_fw(image, image_length + 32));
    registers[IWX_CSR_UCODE_DRV_GP1 / 4] = IWX_GP1_INIT_COMPLETE;
    assert(iwx200_init() && iwx200_online());
    u8 mac[6]; iwx200_mac(mac);
    assert(!memcmp(mac, "\x00\x11\x22\x33\x44\x55", 6));
    assert(pci_command == 0x402); // stub command 0: DMA+memory, I/O cleared
    assert(iwx200_init()); // never retried
    // Command transport round-trip against the emulated firmware.
    {
        u8 response[16] = {0};
        i32 n = iwx200_command(IWX_GRP_SYSTEM, 0x01, 0, NULL, 0, response, sizeof(response));
        assert(n == 4);
        assert(response[0] == 0x78 && response[1] == 0x56 && response[2] == 0x34 && response[3] == 0x12);
        // Oversized payload is refused without touching the rings.
        u8 big[400] = {0};
        assert(iwx200_command(IWX_GRP_SYSTEM, 0x01, 0, big, sizeof(big), response, sizeof(response)) == -90);
        // Offline card refuses.
        card.online = false;
        assert(iwx200_command(IWX_GRP_SYSTEM, 0x01, 0, NULL, 0, response, sizeof(response)) == -22);
        card.online = true;
    }
    // Post-alive init sequence against the emulated firmware.
    {
        assert(iwx200_run_init() == 0);
        u8 mac[6]; iwx200_nvm_mac(mac);
        assert(!memcmp(mac, "\x00\x11\x22\x33\x44\x55", 6));
        card.online = false;
        assert(iwx200_run_init() == -22);
        card.online = true;
    }
    // Open-network association through the MLME orchestrator. The stub
    // acts as firmware (command replies) and as the AP (auth/assoc
    // replies delivered as RX MPDU notifications when mgmt frames go out).
    {
        static const u8 ap[6] = { 0x00, 0x1a, 0x2b, 0x3c, 0x4d, 0x5e };
        assert(iwx200_set_bssid(ap));
        assert(!iwx200_set_bssid(NULL));
        u8 bad[6] = {0};
        assert(!iwx200_set_bssid(bad)); // zero address refused
        assert(iwx200_link() == IWX_LINK_DOWN);
        assert(iwx200_connect("kunix-test", 10, NULL) == 0);
        assert(iwx200_link() == IWX_LINK_UP);
        // Data path loopback: send an Ethernet frame addressed to the
        // station, receive it back through the AP echo.
        u8 station[6]; iwx200_mac(station);
        u8 eth[64];
        memcpy(eth, station, 6); memcpy(eth + 6, station, 6);
        eth[12] = 0x08; eth[13] = 0x00;
        memcpy(eth + 14, "hello", 5);
        assert(iwx200_send(eth, 19));
        u8 back[64] = {0};
        // The stub echoes the TX frame as an RX MPDU on the next poll.
        u32 n = iwx200_receive(back, sizeof(back));
        assert(n == 19 && !memcmp(back, eth, 19));
        // Short frames and oversized frames are refused.
        assert(!iwx200_send(eth, 10));
        assert(iwx200_receive(back, 10) == 0); // nothing waiting
    }
    // A dead bus (all-ones reads) fails bounded.
    memset(&card, 0, sizeof(card)); stuck_clock = true;
    assert(!iwx200_init());
    puts("iwx200 probe, init, assoc, CCMP-ready data path and loopback: PASS");
    return 0;
}
