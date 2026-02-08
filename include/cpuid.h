#ifndef CPUID_H
#define CPUID_H

#include "types.h"

void cpuid_detect(void);
void cpuid_get_vendor(char *vendor);

#endif