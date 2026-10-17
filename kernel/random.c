// This file has been generated in whole or in part by a Large Language Model
// Hardware entropy for getrandom and the random devices. The CPU supplies each
// word directly; simple health checks catch stuck sources, not malicious CPUs.
#include <random.h>
#include <random_hw.h>
#include <errno.h>
#include <io.h>

#define RANDOM_RETRIES 10u
#define RANDOM_MAX_READ 256u
static struct {
    u32 features;
    u64 previous;
    bool checked, have_previous;
} state;

static bool accept_word(u64 word) {
    if (!word || word == ~0ULL || (state.have_previous && word == state.previous)) return false;
    state.previous = word;
    state.have_previous = true;
    return true;
}
static bool next_word(u64* word) {
    if (!state.checked) {
        state.features = random_hw_features();
        state.checked = true;
    }
    if (!state.features) return false;
    for (u32 attempt = 0; attempt < RANDOM_RETRIES; attempt++) {
        if ((state.features & RANDOM_HW_RDRAND) &&
            random_hw_word(false, word) && accept_word(*word)) return true;
        if ((state.features & RANDOM_HW_RDSEED) &&
            random_hw_word(true, word) && accept_word(*word)) return true;
        __asm__ volatile("pause" ::: "memory");
    }
    return false;
}
i64 random_read(void* buffer, u64 length) {
    if (!length) return 0;
    if (!buffer) return -EFAULT;
    if (length > RANDOM_MAX_READ) length = RANDOM_MAX_READ;
    u8* out = buffer;
    u64 done = 0;
    // Serialize the continuous word check across callers on the current CPU.
    // The fixed read size and instruction retry limit bound IRQ exclusion.
    u64 flags = irq_save();
    while (done < length) {
        u64 word;
        if (!next_word(&word)) break;
        u64 amount = length - done;
        if (amount > 8) amount = 8;
        for (u64 i = 0; i < amount; i++) out[done + i] = word >> (8 * i);
        done += amount;
    }
    irq_restore(flags);
    return done ? (i64)done : -EAGAIN;
}
bool random_ready(void) {
    u64 probe;
    return random_read(&probe, sizeof(probe)) == sizeof(probe);
}
