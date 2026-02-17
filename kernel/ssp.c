#include <panic.h>
#include <console.h>

// Canary!
uint64_t __stack_chk_guard = 0xCA5CADECA5CADEAA;

void __stack_chk_fail(void) {
    terminal_printf(WHITE, BLACK, "WATCHDOG VIOLATION: STACK SMASHING DETECTED.\n");
    panic("STACK SMASHING DETECTED", NULL);
}