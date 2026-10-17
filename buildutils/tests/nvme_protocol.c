// This file has been generated in whole or in part by a Large Language Model
// Exercise production queue and translation code with a synthetic controller.
// gcc -Ibuildutils/tests/nvme_stubs -Iinclude -O1 -g -fsanitize=address,undefined
//     buildutils/tests/nvme_protocol.c -o /tmp/nvme_protocol && /tmp/nvme_protocol
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "../../drivers/nvme.c"

kernel_logs_t log_messages[4];
void klog(kernel_logs_t* l, const char* f, ...) { (void)l; (void)f; }
void* pmm_alloc(void) { return NULL; }
uint64_t vmm_hhdm(void) { return 0; }
bool vmm_map(uint64_t v, uint64_t p, uint64_t f) { (void)v; (void)p; (void)f; return false; }
u32 pci_read_config(u8 b, u8 d, u8 f, u8 o) { (void)b; (void)d; (void)f; (void)o; return ~0u; }
void outl(u16 p, u32 v) { (void)p; (void)v; }
void outw(u16 p, u16 v) { (void)p; (void)v; }
u16 inw(u16 p) { (void)p; return 0; }

static u8 registers[8192], bounce[4096], media[32768];
static command_t sq[QUEUE_DEPTH];
static completion_t cq[QUEUE_DEPTH];
static u64 clock_ns;
static u16 seen_tail;
static u32 native_size, submitted;
static int fault;

// The driver's deadline-clock calls give the fake controller a chance to run.
u64 time_counter_ns(void) {
    controller_t* c = &controllers[0];
    queue_t* q = &c->io;
    clock_ns += fault == 3 ? COMMAND_NS : 1000;
    if (!(reg_read(c, 0x14) & 1)) {
        reg_write(c, 0x1c, 0);
        return clock_ns;
    }
    if (q->tail == seen_tail || fault == 3) return clock_ns;
    command_t* cmd = &sq[seen_tail];
    seen_tail = q->tail;
    submitted++;
    u64 lba = ((u64)cmd->cdw11 << 32) | cmd->cdw10;
    u32 size = (cmd->cdw12 + 1) * native_size;
    assert(cmd->nsid == 17);
    assert(cmd->prp2 == 0);
    if ((cmd->cdw0 & 255) != 0) {
        assert(cmd->prp1 == c->bounce_phys);
        assert(size <= 4096 && lba * native_size + size <= sizeof(media));
        if ((cmd->cdw0 & 255) == 1) memcpy(media + lba * native_size, bounce, size);
        else { assert((cmd->cdw0 & 255) == 2); memcpy(bounce, media + lba * native_size, size); }
    }
    cq[q->head] = (completion_t){ .cid = (cmd->cdw0 >> 16) + (fault == 1),
        .sqid = 1, .sqhead = q->tail, .status = q->phase | (fault == 2 ? 4 : 0) };
    return clock_ns;
}
static void setup(u32 size) {
    memset(controllers, 0, sizeof(controllers));
    memset(namespaces, 0, sizeof(namespaces));
    memset(registers, 0, sizeof(registers));
    memset(sq, 0, sizeof(sq)); memset(cq, 0, sizeof(cq));
    memset(bounce, 0, sizeof(bounce));
    for (u32 i = 0; i < sizeof(media); i++) media[i] = (i * 37 + i / 512) & 255;
    clock_ns = 0; seen_tail = 0; fault = 0; submitted = 0; native_size = size;
    controller_t* c = &controllers[0];
    c->regs = registers; c->stride = 4; c->ready_ns = 1000000; c->online = true;
    c->bounce = bounce; c->bounce_phys = 0x100000;
    c->io = (queue_t){ .sq = sq, .cq = cq, .id = 1, .depth = QUEUE_DEPTH, .phase = 1 };
    reg_write(c, 0x14, 1); reg_write(c, 0x1c, 1);
    namespaces[0] = (namespace_t){ .controller = c, .id = 17, .block_size = size,
        .sectors = sizeof(media) / 512 };
    namespace_count = 1;
}
static void translation(u32 size) {
    setup(size);
    u8 before[sizeof(media)], data[17 * 512], output[sizeof(data)];
    memcpy(before, media, sizeof(media));
    memset(data, 0xab, sizeof(data));
    assert(nvme_write(0, 3, 17, data));
    assert(memcmp(media, before, 3 * 512) == 0);
    assert(memcmp(media + 3 * 512, data, sizeof(data)) == 0);
    assert(memcmp(media + 20 * 512, before + 20 * 512, sizeof(media) - 20 * 512) == 0);
    assert(nvme_read(0, 3, 17, output));
    assert(memcmp(output, data, sizeof(data)) == 0);
    assert(nvme_flush(0));
    // More than three queue wraps, including CID rollover.
    controllers[0].io.cid = 65530;
    for (u32 i = 0; i < 80; i++) assert(nvme_read(0, i % 64, 1, output));
    assert(!nvme_read(0, 63, 2, output));
    assert(!nvme_write(0, ~0ULL, 1, data));
    assert(!nvme_read(1, 0, 1, output));
    assert(!nvme_read(0, 0, 1, NULL));
    assert(nvme_read(0, 64, 0, NULL));
    assert(nvme_sectors(0) == 64 && nvme_block_size(0) == size);
}
int main(void) {
    translation(512); translation(4096);
    u8 data[512];
    setup(512); fault = 2;
    assert(!nvme_read(0, 0, 1, data));
    assert(controllers[0].online); // ordinary command errors retire safely
    fault = 0; assert(nvme_read(0, 0, 1, data));
    setup(512); fault = 1;
    assert(!nvme_read(0, 0, 1, data));
    assert(!controllers[0].online);
    u32 prior = submitted;
    assert(!nvme_read(0, 0, 1, data) && submitted == prior);
    setup(4096); fault = 3;
    assert(!nvme_write(0, 1, 1, data));
    assert(!controllers[0].online);
    assert(controllers[0].bounce == bounce); // quarantine remains owned
    puts("nvme_protocol: translation, wraps, CID, errors, deadlines PASS");
    return 0;
}
