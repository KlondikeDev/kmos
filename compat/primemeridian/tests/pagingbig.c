// This file has been generated in whole or in part by a Large Language Model
// Large zero-fill segment: exec must reserve it without allocating it all.
#include <unistd.h>
#include <stdio.h>
static volatile unsigned char bss[192 * 1024 * 1024];
int main(void) {
    if (bss[0] || bss[sizeof(bss)-1]) return 1;
    FILE* status = fopen("/proc/self/status", "r");
    char line[160]; long resident = -1;
    if (!status) return 3;
    while (fgets(line, sizeof(line), status))
        if (sscanf(line, "VmRSS: %ld kB", &resident) == 1) break;
    fclose(status);
    if (resident < 0 || resident > 4096) return 4;
    bss[0] = 17; bss[sizeof(bss)-1] = 31;
    return bss[0] == 17 && bss[sizeof(bss)-1] == 31 ? 42 : 2;
}
