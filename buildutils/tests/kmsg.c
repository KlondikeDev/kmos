// This file has been generated in whole or in part by a Large Language Model
#include <stdio.h>
#include "../../drivers/serial.c"
#include "../../kernel/klog.c"
static char snapshot[KMSG_CAPACITY];
int main(void) {
    init_serial();
    if (faulty || uart_status_reads < 3) {
        puts("FAIL UART loopback response was read before it arrived");
        return 10;
    }
    uart_writes = 0;
    faulty = true;
    kserialout("no UART\n");
    if (kmsg_read(0, snapshot, sizeof(snapshot)) != 8 || uart_writes) return 1;
    if (snapshot[0] != 'n' || snapshot[7] != '\n') return 2;
    faulty = false;
    kserialputchar('\n');
    if (uart_writes != 2 || uart_bytes[0] != '\r' || uart_bytes[1] != '\n') return 3;
    if (kmsg_read(0, snapshot, sizeof(snapshot)) != 9) return 4; // no synthetic CR
    for (u32 i = 0; i < KMSG_CAPACITY + 100; i++) kmsg_putc('a');
    kmsg_putc('z');
    if (kmsg_read(0, snapshot, sizeof(snapshot)) != KMSG_CAPACITY) return 5;
    if (snapshot[0] != 'a' || snapshot[KMSG_CAPACITY - 1] != 'z') return 6;
    if (kmsg_tail(snapshot, 2) != 2 || snapshot[0] != 'a' || snapshot[1] != 'z') return 7;
    if (kmsg_read(KMSG_CAPACITY, snapshot, 1) != 0) return 8;
    if (kmsg_read(~0ULL, snapshot, 1) != 0) return 9;
    puts("kmsg: passed (no UART, CRLF, wraparound, bounds)");
    return 0;
}
