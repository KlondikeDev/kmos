// This file has been generated in whole or in part by a Large Language Model
#pragma once
#include <types.h>

void acpi_init(void);
bool acpi_poweroff(void);
bool acpi_reset(void);
bool acpi_find_s5(const u8* aml, u64 size, u16* a, u16* b);
