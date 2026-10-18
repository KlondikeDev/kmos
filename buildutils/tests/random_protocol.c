// This file has been generated in whole or in part by a Large Language Model
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/random.c"
static u32 hardware, calls;
static bool carry, fail_rdrand;
static u64 words[32];
static u32 index_word, count_word;
u32 random_hw_features(void) { return hardware; }
bool random_hw_word(bool seed, u64* out) {
    calls++;
    if (!carry || (!seed && fail_rdrand)) return false;
    *out = words[index_word < count_word ? index_word++ : count_word - 1];
    return true;
}
static void setup(u32 features) {
    memset(&state, 0, sizeof(state)); hardware = features; calls = 0;
    carry = true; fail_rdrand = false; index_word = 0; count_word = 2;
    words[0] = 0x123456789abcdef0ULL; words[1] = 0x23456789abcdef01ULL;
}
int main(void) {
    u8 output[300], before[300];
    memset(output, 0xa5, sizeof(output)); memcpy(before, output, sizeof(output));
    setup(0); assert(random_read(output, 32) == -EAGAIN && calls == 0);
    assert(!random_ready()); assert(memcmp(output, before, sizeof(output)) == 0);
    assert(random_read(NULL, 0) == 0); assert(random_read(NULL, 1) == -EFAULT);
    setup(RANDOM_HW_RDRAND); carry = false;
    assert(random_read(output, 8) == -EAGAIN && calls == RANDOM_RETRIES);
    assert(memcmp(output, before, sizeof(output)) == 0);
    setup(RANDOM_HW_RDRAND); words[0] = 0; words[1] = ~0ULL;
    assert(random_read(output, 8) == -EAGAIN && calls == RANDOM_RETRIES);
    setup(RANDOM_HW_RDRAND); count_word = 1;
    assert(random_read(output, 16) == 8);
    assert(memcmp(output, words, 8) == 0 && output[8] == 0xa5);
    assert(random_read(output, 8) == -EAGAIN); // repeated across calls is rejected
    setup(RANDOM_HW_RDRAND); assert(random_read(output + 1, 9) == 9);
    assert(output[0] == 0xf0 && output[1] == 0xf0 && output[9] == 1);
    setup(RANDOM_HW_RDRAND | RANDOM_HW_RDSEED); fail_rdrand = true;
    assert(random_read(output, 8) == 8 && calls == 2);
    setup(RANDOM_HW_RDSEED); assert(random_ready());
    setup(RANDOM_HW_RDRAND);
    count_word = 32; for (u32 i = 0; i < 32; i++) words[i] = 0x1234567800000001ULL + i;
    memset(output, 0xa5, sizeof(output));
    assert(random_read(output, sizeof(output)) == RANDOM_MAX_READ);
    for (u32 i = RANDOM_MAX_READ; i < sizeof(output); i++) assert(output[i] == 0xa5);
    puts("random: features, carry, retries, health checks, fallback and partial bounds PASS");
}
