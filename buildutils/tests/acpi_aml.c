// This file has been generated in whole or in part by a Large Language Model
#include <stdio.h>
#include <acpi.h>

static int failed;
#define CHECK(name, expr) do { if (!(expr)) { printf("FAIL %s\n", name); failed++; } } while (0)
int main(void) {
    u16 a = 0, b = 0;
    const u8 valid[] = { 0x08, '\\', '_', 'S', '5', '_', 0x12, 8, 4, 0x0a, 5, 0x0a, 6, 0, 0 };
    CHECK("S5 byte constants", acpi_find_s5(valid, sizeof(valid), &a, &b) && a == (5 << 10) && b == (6 << 10));
    const u8 small[] = { 0x08, '_', 'S', '5', '_', 0x12, 4, 2, 0, 1 };
    CHECK("S5 zero/one constants", acpi_find_s5(small, sizeof(small), &a, &b) && a == 0 && b == (1 << 10));
    for (u64 n = 0; n < sizeof(valid); n++) {
        CHECK("truncated package rejected", !acpi_find_s5(valid, n, &a, &b));
    }
    const u8 bad[] = { 0x08, '_', 'S', '5', '_', 0x12, 6, 2, 0x0a, 8, 0x0a, 2 };
    CHECK("invalid sleep type", !acpi_find_s5(bad, sizeof(bad), &a, &b));
    const u8 text[] = { '_', 'S', '5', '_', 0x12, 4, 2, 0, 1 };
    CHECK("string without NameOp", !acpi_find_s5(text, sizeof(text), &a, &b));
    const u8 extended[] = { 0x08, '_', 'S', '5', '_', 0x12, 0x45, 0, 2, 1, 0 };
    CHECK("multibyte package length", acpi_find_s5(extended, sizeof(extended), &a, &b) && a == (1 << 10) && b == 0);
    printf("acpi AML: %s\n", failed ? "FAILED" : "passed");
    return failed != 0;
}
