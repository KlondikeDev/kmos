// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#ifndef CPUID_H
#define CPUID_H

#include "types.h"

void cpuid_detect(void);
void cpuid_get_vendor(char *vendor);

#endif