// This file has been generated in whole or in part by a Large Language Model
#pragma once
#include <types.h>
#define LWIP_NO_STDINT_H 1
#define LWIP_NO_INTTYPES_H 1
#define LWIP_NO_LIMITS_H 1
#define LWIP_NO_CTYPE_H 1
#define LWIP_NO_UNISTD_H 1
typedef u8 u8_t; typedef i8 s8_t; typedef u16 u16_t; typedef i16 s16_t;
typedef u32 u32_t; typedef i32 s32_t; typedef uintptr_t mem_ptr_t;
#define BYTE_ORDER LITTLE_ENDIAN
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define LWIP_PLATFORM_DIAG(x) do {} while(0)
#define LWIP_PLATFORM_ASSERT(x) do { __builtin_trap(); } while(0)
#define LWIP_RAND() kunix_lwip_rand()
u32 kunix_lwip_rand(void);
