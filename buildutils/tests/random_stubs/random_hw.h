// This file has been generated in whole or in part by a Large Language Model
#pragma once
#include <types.h>
#define RANDOM_HW_RDRAND 1u
#define RANDOM_HW_RDSEED 2u
u32 random_hw_features(void);
bool random_hw_word(bool seed, u64* out);
