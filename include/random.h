// This file has been generated in whole or in part by a Large Language Model
#pragma once
#include <types.h>
// Hardware random source only. No clock, address or weak PRNG fallback.
// At most 256 bytes per call; partial reads are valid. Unavailable/exhausted
// hardware returns -EAGAIN when no bytes were produced.
i64 random_read(void* buffer, u64 length);
bool random_ready(void);
