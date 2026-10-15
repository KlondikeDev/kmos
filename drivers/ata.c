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
