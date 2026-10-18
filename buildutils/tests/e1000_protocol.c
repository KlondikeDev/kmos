// This file has been generated in whole or in part by a Large Language Model
// gcc -Ibuildutils/tests/e1000_stubs -Iinclude -Wall -Wextra -Werror
//     -fsanitize=address,undefined buildutils/tests/e1000_protocol.c -o /tmp/e1000_protocol
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../drivers/e1000.c"

static u32 registers[0x6000 / 4];
static rx_desc_t rx_ring[RING_COUNT];
static tx_desc_t tx_ring[RING_COUNT];
static u8 rx_data[RING_COUNT][2048], tx_data[RING_COUNT][2048];
static u64 clock_ns;
static bool stuck_reset;
static u32 pci_address;
static u16 pci_command;
static bool present;
static u32 pages;
void outl(u16 port, u32 value) { assert(port == PCI_CONFIG_ADDRESS); pci_address = value; }
void outw(u16 port, u16 value) { assert(port == PCI_CONFIG_DATA); pci_command = value; }
pci_device_t pci_scan_for_device(u16 v, u16 d) { (void)v; (void)d; return present ? (pci_device_t){ .vendor_id = v, .device_id = d } : (pci_device_t){0}; }
u32 pci_read_config(u8 b, u8 d, u8 f, u8 off) { (void)b; (void)d; (void)f; return off == 0x10 ? 0xf0000000u : 0; }
void* pmm_alloc(void) { pages++; return aligned_alloc(4096, 4096); }
uint64_t vmm_hhdm(void) { return 0; }
bool vmm_map(uint64_t v, uint64_t p, uint64_t f) { assert(v >= MMIO_BASE && v < MMIO_BASE + MMIO_SIZE);
    assert(p >= 0xf0000000u && p < 0xf0020000u); assert(f == 0x1b);
    nic.regs = (volatile u8*)registers; return true; }
u64 time_counter_ns(void) {
    clock_ns += 1000000;
    if (!stuck_reset) registers[0] &= ~(1u << 26);
    return clock_ns;
}
static void setup(void) {
    memset(&nic, 0, sizeof(nic)); memset(registers, 0, sizeof(registers));
    memset(rx_ring, 0, sizeof(rx_ring)); memset(tx_ring, 0, sizeof(tx_ring));
    nic.regs = (volatile u8*)registers; nic.rx = rx_ring; nic.tx = tx_ring;
    nic.online = true;
    for (u32 i = 0; i < RING_COUNT; i++) {
        nic.rx_buffers[i] = rx_data[i]; nic.tx_buffers[i] = tx_data[i]; tx_ring[i].status = 1;
    }
    registers[8 / 4] = 2;
}
int main(void) {
    setup(); u8 packet[1514], output[1514]; memset(packet, 0x5a, sizeof(packet));
    assert(e1000_link()); registers[2] = 0; assert(!e1000_link()); registers[2] = 2;
    assert(!e1000_send(NULL, 60) && !e1000_send(packet, 13) && !e1000_send(packet, 1515));
    for (u32 i = 0; i < RING_COUNT - 1; i++) {
        assert(e1000_send(packet, sizeof(packet)));
        assert(tx_ring[i].length == sizeof(packet) && tx_ring[i].status == 0);
        assert((tx_ring[i].command & 0xb) == 0xb);
        assert(memcmp(tx_data[i], packet, sizeof(packet)) == 0);
    }
    assert(!e1000_send(packet, sizeof(packet)));
    registers[0x3810 / 4] = 1; assert(e1000_send(packet, 60)); assert(nic.tx_next == 0);
    tx_ring[0].status = 1; registers[0x3810 / 4] = 2;
    assert(e1000_send(packet, 60)); assert(nic.tx_next == 1);
    assert(e1000_receive(output, sizeof(output)) == 0);
    for (u32 i = 0; i < RING_COUNT * 3; i++) {
        u32 n = i % RING_COUNT; memcpy(rx_data[n], packet, 60);
        rx_ring[n].length = 60; rx_ring[n].status = 3;
        assert(e1000_receive(output, sizeof(output)) == 60);
        assert(memcmp(output, packet, 60) == 0 && rx_ring[n].status == 0);
        assert(registers[0x2818 / 4] == n);
    }
    // A chain split across polls must be discarded through its final EOP.
    rx_ring[0].status = 1; rx_ring[0].length = 60;
    assert(e1000_receive(output, sizeof(output)) == 0);
    rx_ring[1].status = 3; rx_ring[1].length = 60;
    assert(e1000_receive(output, sizeof(output)) == 0);
    rx_ring[2].status = 3; rx_ring[2].length = 60; rx_ring[2].errors = 1;
    rx_ring[3].status = 3; rx_ring[3].length = 2049;
    rx_ring[4].status = 3; rx_ring[4].length = 13;
    rx_ring[5].status = 3; rx_ring[5].length = 60;
    assert(e1000_receive(output, 59) == 0);
    assert(nic.rx_next == 6);
    rx_ring[6].status = 3; rx_ring[6].length = 60;
    assert(e1000_receive(NULL, 100) == 0 && rx_ring[6].status == 3);
    assert(e1000_receive(output, sizeof(output)) == 60);
    clock_ns = 0; stuck_reset = false; registers[0] = 1u << 26; assert(wait_reset());
    clock_ns = 0; stuck_reset = true; registers[0] = 1u << 26; assert(!wait_reset());
    assert(clock_ns >= RESET_NS && clock_ns < RESET_NS + 5000000);
    pci_device_t dev = { .bus = 2, .device = 3, .function = 1 };
    command_write(&dev, 0x406); assert(pci_command == 0x406);
    assert(pci_address == (0x80000000u | (2u << 16) | (3u << 11) | (1u << 8) | 4));
    nic.online = false; assert(!e1000_send(packet, 60)); assert(!e1000_link());
    assert(e1000_receive(output, sizeof(output)) == 0);
    // Exercise complete init with fake PCI/MMIO and real aligned DMA pages.
    memset(&nic, 0, sizeof(nic)); present = false; assert(!e1000_init());
    memset(&nic, 0, sizeof(nic)); present = true; stuck_reset = false;
    registers[0x5400 / 4] = 0x12005452; registers[0x5404 / 4] = 0x80005634;
    assert(e1000_init()); assert(pages == 2 + 2 * RING_COUNT);
    u8 mac[6]; e1000_mac(mac); assert(memcmp(mac, "\x52\x54\x00\x12\x34\x56", 6) == 0);
    assert(pci_command == 0x406); assert(registers[0x2808 / 4] == RING_COUNT * 16);
    assert(registers[0x2818 / 4] == RING_COUNT - 1);
    assert(e1000_init() && pages == 2 + 2 * RING_COUNT);
    puts("e1000 descriptor ownership, wrap, malformed RX, bounds and deadlines: PASS");
}
