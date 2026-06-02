// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <pci.h>
#include <io.h>
#include <klog.h>
#include <console.h>

// PCI_CONFIG_ADDRESS  0xCF8
// PCI_CONFIG_DATA     0xCFC

// PCI_VENDOR_INTEL    0x8086
// PCI_DEVICE_E1000    0x100E


uint32_t pci_read_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t address = (1u << 31) | (bus << 16) | (dev << 11) | (func << 8) | (offset & 0xFC);

    outl(PCI_CONFIG_ADDRESS, address);

    uint32_t data = inl(PCI_CONFIG_DATA);

    return data;
}

pci_device_t pci_scan_for_device(uint16_t vendor, uint16_t device) {
    pci_device_t result = {0};  // Initialize to zeros
    
    for(int i = 0; i < 256; i++){
        for(int j = 0; j < 32; j++){
            for(int k = 0; k < 8; k++){
                uint32_t data = pci_read_config(i, j, k, 0);
                uint16_t vendor_id = data & 0xFFFF;
                uint16_t device_id = (data >> 16) & 0xFFFF;
                
                if(vendor_id == 0xFFFF){
                    continue;
                }
                if(vendor_id == vendor && device_id == device){
                    result.bus = i;
                    result.device = j;
                    result.function = k;
                    result.vendor_id = vendor_id;
                    result.device_id = device_id;
                    return result;
                }
            }
        }
    }
    
    return result;
}

uint32_t pci_read_bar(pci_device_t* dev, uint8_t bar_num) {
    uint8_t offset = 0x10 + (bar_num * 4);
    return pci_read_config(dev->bus, dev->device, dev->function, offset);
}

void pci_list_devices(void) {
    for(int i = 0; i < 256; i++){
        for(int j = 0; j < 32; j++){
            for(int k = 0; k < 8; k++){
                uint32_t data = pci_read_config(i, j, k, 0);
                uint16_t vendor_id = data & 0xFFFF;
                uint16_t device_id = (data >> 16) & 0xFFFF;
                
                if(vendor_id == 0xFFFF){
                    continue;
                } else {
                    uint32_t class_data = pci_read_config(i, j, k, 0x08);
                    uint8_t class_code = (class_data >> 24) & 0xFF;
                    uint8_t subclass = (class_data >> 16) & 0xFF;
                    terminal_printf(SILVER, BLACK, "Bus %d, Device %d, Func %d: ", i, j, k);
                    terminal_printf(SILVER, BLACK, "Vendor=%s Device=0x%x Class=%s.0x%x\n", pci_vendor_name(vendor_id), device_id, pci_class_name(class_code), subclass);
                }
            }
        }
    }
    klog(LOG_OKAY, "PCI scan completed.\n");
}

const char* pci_vendor_name(uint16_t vendor_id) {
    switch(vendor_id) {
        case 0x8086: return "Intel";
        case 0x1234: return "QEMU";
        case 0x10EC: return "Realtek";
        case 0x1022: return "AMD";
        case 0x10DE: return "NVIDIA";
        default: return "Unknown";
    }
}

const char* pci_class_name(uint8_t class_code) {
    switch(class_code) {
        case 0x00: return "Unclassified";
        case 0x01: return "Mass Storage";
        case 0x02: return "Network";
        case 0x03: return "Display";
        case 0x06: return "Bridge";
        default: return "Other";
    }
}