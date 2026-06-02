// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>

#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

#define PCI_VENDOR_INTEL    0x8086
#define PCI_DEVICE_E1000    0x100E

typedef struct {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    uint16_t vendor_id;
    uint16_t device_id;
} pci_device_t;

uint32_t pci_read_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
pci_device_t pci_scan_for_device(uint16_t vendor, uint16_t device);
uint32_t pci_read_bar(pci_device_t* dev, uint8_t bar_num);
void pci_list_devices(void);
const char* pci_class_name(uint8_t class_code);
const char* pci_vendor_name(uint16_t vendor_id);