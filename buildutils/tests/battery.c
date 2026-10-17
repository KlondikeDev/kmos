// This file has been generated in whole or in part by a Large Language Model
#include <battery.h>
#include <assert.h>
int main(void) {
    battery_info_t b;
    u32 bst[4] = {1, 10000, 25000, 12000};
    battery_normalize(&b, true, 0, 50000, bst);
    assert(b.percent == 50 && b.state == BAT_DISCHARGING && !b.critical);
    bst[0] = 2; battery_normalize(&b, true, 1, 50000, bst);
    assert(b.state == BAT_CHARGING && b.unit == 1);
    bst[0] = 0; bst[2] = 50000;
    battery_normalize(&b, true, 0, 50000, bst); assert(b.state == BAT_FULL);
    bst[2] = 49000; battery_normalize(&b, true, 0, 50000, bst);
    assert(b.state == BAT_IDLE && b.percent == 98);
    bst[0] = 5; bst[2] = 1000;
    battery_normalize(&b, true, 0, 50000, bst); assert(b.critical && b.percent == 2);
    bst[2] = BATTERY_UNKNOWN;
    battery_normalize(&b, true, 0, 50000, bst); assert(b.percent == -1);
    bst[2] = 1; battery_normalize(&b, true, 0, 0, bst); assert(b.percent == -1);
    bst[2] = 0xfffffffe; battery_normalize(&b, true, 0, 0xfffffffe, bst);
    assert(b.percent == 100); // multiplication must not overflow 32 bits
    battery_normalize(&b, false, 0, 50000, bst); assert(!b.present && !b.valid && b.percent == -1);
    bst[0] = 3; battery_normalize(&b, true, 0, 50000, bst); assert(b.state == BAT_UNKNOWN);
    battery_normalize(&b, true, 2, 50000, bst); assert(!b.valid && b.percent == -1);
    return 0;
}
