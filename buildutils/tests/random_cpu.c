// This file has been generated in whole or in part by a Large Language Model
// Links the production service and executes the host CPU's real instructions.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <random.h>
#include <random_hw.h>
#include <errno.h>
int main(void) {
    u8 bytes[256], next[256];
    u32 features = random_hw_features();
    if (!features) {
        assert(!random_ready()); assert(random_read(bytes, sizeof(bytes)) == -EAGAIN);
        puts("random CPU: no supported entropy instructions; safe EAGAIN PASS");
        return 0;
    }
    assert(random_ready());
    assert(random_read(bytes, sizeof(bytes)) == sizeof(bytes));
    assert(random_read(next, sizeof(next)) == sizeof(next));
    assert(memcmp(bytes, next, sizeof(bytes)) != 0);
    printf("random CPU: CPUID features %u, two real 256-byte reads PASS\n", features);
}
