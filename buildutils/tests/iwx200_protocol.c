// This file has been generated in whole or in part by a Large Language Model
// gcc -Ibuildutils/tests/iwx200_stubs -Iinclude -Iinclude/klibc -Wall -Wextra
//     -Werror -fsanitize=address,undefined buildutils/tests/iwx200_protocol.c
//     -o /tmp/iwx200_protocol
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../drivers/iwx200.c"

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
void* pmm_alloc(void) { return aligned_alloc(4096, 4096); }
uint64_t vmm_hhdm(void) { return 0; }
bool vmm_map(uint64_t v, uint64_t p, uint64_t f) {
    assert(v >= MMIO_BASE && v < MMIO_BASE + MMIO_SIZE);
    assert(p >= 0xf0000000u && p < 0xf0020000u); assert(f == 0x1b);
    card.regs = (volatile u8*)registers; return true;
}
u64 time_counter_ns(void) {
    clock_ns += 1000000;
    if (stuck_clock) registers[IWX_CSR_HW_REV / 4] = ~0u;
    // The card raises its clock-ready bit once the host sets INIT_DONE.
    if (registers[IWX_CSR_GP_CNTRL / 4] & IWX_GP_INIT_DONE)
        registers[IWX_CSR_GP_CNTRL / 4] |= IWX_GP_MAC_CLOCK_READY;
    return clock_ns;
}
// Minimal valid TLV image: header plus one INIT section TLV with a 64-byte
// payload (device offset word + 60 bytes of code).
static u32 build_image(u8* image) {
    memset(image, 0, 256);
    image[4] = 0x49; image[5] = 0x57; image[6] = 0x4c; image[7] = 0x0a;
    image[72] = 77; // API version 77
    image[104] = IWX_TLV_SEC_INIT; image[108] = 64;
    image[112] = 0x40; image[113] = 0x00; image[114] = 0x00; image[115] = 0x00;
    for (u32 i = 116; i < 176; i++) image[i] = (u8)i;
    return 176;
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
    assert(fw.api == 77 && fw.init_count == 1 && fw.init[0].length == 60);
    image[108] = 200; assert(!iwx200_parse_fw(image, image_length, &fw)); // overrun
    image[108] = 64;
    assert(iwx200_parse_fw(image, image_length, &fw));
    // Separator markers split sections without ending parsing.
    image[120] = 0xcc; image[121] = 0xcc; image[122] = 0xff; image[123] = 0xff;
    assert(iwx200_parse_fw(image, image_length, &fw) && fw.init_count == 2);
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
    assert(iwx200_stage_fw(image, image_length));
    assert(!iwx200_init());
    // Alive announcement completes init.
    memset(&card, 0, sizeof(card));
    assert(iwx200_stage_fw(image, image_length));
    registers[IWX_CSR_UCODE_DRV_GP1 / 4] = IWX_GP1_INIT_COMPLETE;
    assert(iwx200_init() && iwx200_online());
    u8 mac[6]; iwx200_mac(mac);
    assert(!memcmp(mac, "\x00\x11\x22\x33\x44\x55", 6));
    assert(pci_command == 0x402); // stub command 0: DMA+memory, I/O cleared
    assert(iwx200_init()); // never retried
    // A dead bus (all-ones reads) fails bounded.
    memset(&card, 0, sizeof(card)); stuck_clock = true;
    assert(!iwx200_init());
    puts("iwx200 PCI probe, CSR init, firmware TLV and alive handshake: PASS");
    return 0;
}
