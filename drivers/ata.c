// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <console.h>
#include <types.h>
#include <ata.h>
#include <klog.h>
#include <io.h>

uint16_t identify_data[256];

bool ata_poll(){
    uint8_t status = inb(ATA_REG_STATUS);

    if(status == 0 || status == 0xFF){
        klog(LOG_ERROR, "No drive found...\n");
        return false;
    }

    do {
        status = inb(ATA_REG_STATUS);
        if(status & ATA_STATUS_ERR){
            klog(LOG_ERROR, "Drive error...\n");
            return false;
        }
    } while((status & ATA_STATUS_BUSY) || !(status & ATA_STATUS_DRQ));

    return true;
}

void ata_print_model(void){
    char model[41];
    for(int i = 0; i < 20; i++){
        uint16_t word = identify_data[27 + i];
        model[i * 2]     = (char)(word >> 8);
        model[i * 2 + 1] = (char)(word & 0xFF);
    }
    model[40] = '\0';

    for(int i = 39; i >= 0 && model[i] == ' '; i--){
        model[i] = '\0';
    }

    klog(LOG_OKAY, "ATA drive model: %s\n", model);
}

bool identify_drive(uint8_t drive_select){
    outb(ATA_REG_DRIVE, drive_select);
    for(int i = 0; i < 4; i++){
        inb(ATA_REG_ALT_STATUS);
    }
    outb(ATA_REG_SECTOR_COUNT, 0);
    outb(ATA_REG_LBA_LOW, 0);
    outb(ATA_REG_LBA_MID, 0);
    outb(ATA_REG_LBA_HI, 0);
    outb(ATA_REG_STATUS, ATA_CMD_IDENTIFY);

    if(ata_poll()) {
        for(int i = 0; i < 256; i++) {
            identify_data[i] = inw(ATA_REG_DATA);
        }
        klog(LOG_OKAY, "IDENTIFY data read successfully\n");
        ata_print_model();
        return true;
    }
    return false;
}

// drive: 0 = primary master, 1 = primary slave.
bool ata_read(u8 drive, u32 lba, void* buffer) {
    while(inb(ATA_REG_STATUS) & ATA_STATUS_BUSY);

    // Bit 4 of the drive register picks master/slave. After switching drives
    // the status register needs ~400ns to reflect the new one, which is four
    // reads of the alternate status port.
    outb(ATA_REG_DRIVE, 0xE0 | ((drive & 1) << 4) | ((lba >> 24) & 0x0F));
    for(int i = 0; i < 4; i++){
        inb(ATA_REG_ALT_STATUS);
    }
    outb(ATA_REG_SECTOR_COUNT, 1);
    outb(ATA_REG_LBA_LOW, lba & 0xFF);
    outb(ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_REG_LBA_HI, (lba >> 16) & 0xFF);

    outb(ATA_REG_STATUS, 0x20);

    if(!ata_poll()) {
        return false;
    }

    uint16_t* data = (uint16_t*)buffer;
    for(int i = 0; i < 256; i++) {
        data[i] = inw(ATA_REG_DATA);
    }
    return true;
}

bool read_sectors(u32 lba, void* buffer) {
    return ata_read(0, lba, buffer);
}

// Write one 512-byte sector, then flush the drive's write cache so the data
// is really on the disk (or in QEMU's image) before we report success.
bool ata_write(u8 drive, u32 lba, const void* buffer) {
    while(inb(ATA_REG_STATUS) & ATA_STATUS_BUSY);

    outb(ATA_REG_DRIVE, 0xE0 | ((drive & 1) << 4) | ((lba >> 24) & 0x0F));
    for(int i = 0; i < 4; i++){
        inb(ATA_REG_ALT_STATUS);
    }
    outb(ATA_REG_SECTOR_COUNT, 1);
    outb(ATA_REG_LBA_LOW, lba & 0xFF);
    outb(ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_REG_LBA_HI, (lba >> 16) & 0xFF);

    outb(ATA_REG_STATUS, ATA_CMD_WRITE);

    if(!ata_poll()) {
        return false;
    }

    const uint16_t* data = (const uint16_t*)buffer;
    for(int i = 0; i < 256; i++) {
        outw(ATA_REG_DATA, data[i]);
    }

    outb(ATA_REG_STATUS, ATA_CMD_FLUSH);
    uint8_t status;
    do {
        status = inb(ATA_REG_STATUS);
    } while(status & ATA_STATUS_BUSY);
    return !(status & ATA_STATUS_ERR);
}

bool write_sectors(u32 lba, const void* buffer) {
    return ata_write(0, lba, buffer);
}

// ---------------------------------------------------------------------------
// Multi-sector transfers, any of the four IDE drives. (These are the ones
// filesystems with bigger blocks use: one command moves a whole block, and
// writes leave the flush to the caller, once per operation.)
// ---------------------------------------------------------------------------

// drive: 0/1 = primary master/slave, 2/3 = secondary master/slave.
static u16 ata_io(u8 drive)  { return drive < 2 ? 0x1F0 : 0x170; }
static u16 ata_ctl(u8 drive) { return drive < 2 ? 0x3F6 : 0x376; }

static bool ata_wait_drq(u16 io) {
    uint8_t status;
    do {
        status = inb(io + 7);
        if (status & ATA_STATUS_ERR) return false;
        if (status == 0 || status == 0xFF) return false;          // nothing there
    } while ((status & ATA_STATUS_BUSY) || !(status & ATA_STATUS_DRQ));
    return true;
}

static void ata_select(u8 drive, u32 lba, u8 count) {
    u16 io = ata_io(drive);
    while (inb(io + 7) & ATA_STATUS_BUSY);
    outb(io + 6, 0xE0 | ((drive & 1) << 4) | ((lba >> 24) & 0x0F));
    for (int i = 0; i < 4; i++) inb(ata_ctl(drive));               // 400 ns settle
    outb(io + 2, count);                                           // 0 means 256
    outb(io + 3, lba & 0xFF);
    outb(io + 4, (lba >> 8) & 0xFF);
    outb(io + 5, (lba >> 16) & 0xFF);
}

bool ata_read_multi(u8 drive, u32 lba, u32 count, void* buffer) {
    u16* data = (u16*)buffer;
    while (count) {
        u32 n = count > 256 ? 256 : count;
        ata_select(drive, lba, (u8)n);
        outb(ata_io(drive) + 7, 0x20);                             // READ SECTORS
        for (u32 s = 0; s < n; s++) {
            if (!ata_wait_drq(ata_io(drive))) return false;
            for (int i = 0; i < 256; i++) *data++ = inw(ata_io(drive));
        }
        lba += n;
        count -= n;
    }
    return true;
}

bool ata_write_multi(u8 drive, u32 lba, u32 count, const void* buffer) {
    const u16* data = (const u16*)buffer;
    while (count) {
        u32 n = count > 256 ? 256 : count;
        ata_select(drive, lba, (u8)n);
        outb(ata_io(drive) + 7, ATA_CMD_WRITE);                    // WRITE SECTORS
        for (u32 s = 0; s < n; s++) {
            if (!ata_wait_drq(ata_io(drive))) return false;
            for (int i = 0; i < 256; i++) outw(ata_io(drive), *data++);
        }
        // Wait for the last sector to be taken before the next command.
        uint8_t status;
        do { status = inb(ata_io(drive) + 7); } while (status & ATA_STATUS_BUSY);
        if (status & ATA_STATUS_ERR) return false;
        lba += n;
        count -= n;
    }
    return true;
}

bool ata_flush(u8 drive) {
    u16 io = ata_io(drive);
    while (inb(io + 7) & ATA_STATUS_BUSY);
    outb(io + 6, 0xE0 | ((drive & 1) << 4));
    for (int i = 0; i < 4; i++) inb(ata_ctl(drive));
    outb(io + 7, ATA_CMD_FLUSH);
    uint8_t status;
    do { status = inb(io + 7); } while (status & ATA_STATUS_BUSY);
    return !(status & ATA_STATUS_ERR);
}

// IDENTIFY any of the four drives; true if an ATA disk answers.
bool ata_present(u8 drive) {
    u16 io = ata_io(drive);
    outb(io + 6, 0xA0 | ((drive & 1) << 4));
    for (int i = 0; i < 4; i++) inb(ata_ctl(drive));
    outb(io + 2, 0);
    outb(io + 3, 0);
    outb(io + 4, 0);
    outb(io + 5, 0);
    outb(io + 7, ATA_CMD_IDENTIFY);
    if (inb(io + 7) == 0) return false;                            // no drive
    // ATAPI (the CD-ROM) and SATA bridges announce themselves in LBA mid/hi.
    uint8_t status;
    do {
        status = inb(io + 7);
        if (inb(io + 4) || inb(io + 5)) return false;              // not ATA
    } while (status & ATA_STATUS_BUSY);
    if (!ata_wait_drq(io)) return false;
    for (int i = 0; i < 256; i++) identify_data[i] = inw(io);
    return true;
}

// LBA28 capacity is discovered once, alongside IDENTIFY, and never probed on
// cache hits. Zero also remembers an absent device.
u64 ata_sector_count(u8 drive) {
    static u64 sectors[4];
    static bool probed[4];
    if (drive >= 4) return 0;
    if (!probed[drive]) {
        probed[drive] = true;
        if (ata_present(drive)) {
            u64 n = (u64)identify_data[60] | ((u64)identify_data[61] << 16);
            sectors[drive] = n > (1ULL << 28) ? (1ULL << 28) : n;
        }
    }
    return sectors[drive];
}
