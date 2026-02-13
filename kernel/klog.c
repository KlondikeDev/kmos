#include <types.h>
#include <console.h>
#include <serial.h>
#include <klog.h>
#include <pit.h>

kernel_logs_t log_messages[4] = {
    {"OKAY", 0, GREEN},
    {"WARN", 1, YELLOW},
    {"DEBUG", 2, BLUE},
    {"ERROR", 3, RED}
};

void klog(kernel_logs_t* log, const char* msg) {
    kserialout(msg);
    kserialout("\n");

    u32 seconds = ticks / 250;
    u32 milliseconds = (ticks % 250) * 4;

    terminal_printf(SILVER, BLACK, "[%d.%d] ", seconds, milliseconds);
    terminal_printf(SILVER, BLACK, "[");
    terminal_printf(log->color, BLACK, "%s", log->status_msg);
    terminal_printf(SILVER, BLACK, "] ");
    terminal_printf(SILVER, BLACK, "%s\n", msg);
}