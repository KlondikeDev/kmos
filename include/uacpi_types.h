// This file has been generated in whole or in part by a Large Language Model
#pragma once
#include <types.h>
#include <stdarg.h>
#include <uacpi/helpers.h>
// Keep Kunix's u8 bool without importing stdbool's macro into kernel headers.
typedef u8 uacpi_u8;
typedef u16 uacpi_u16;
typedef u32 uacpi_u32;
typedef u64 uacpi_u64;
typedef i8 uacpi_i8;
typedef i16 uacpi_i16;
typedef i32 uacpi_i32;
typedef i64 uacpi_i64;
typedef bool uacpi_bool;
typedef uintptr_t uacpi_uintptr;
typedef u64 uacpi_virt_addr;
typedef size_t uacpi_size;
typedef char uacpi_char;
typedef va_list uacpi_va_list;
#define UACPI_TRUE true
#define UACPI_FALSE false
#define UACPI_NULL NULL
#define uacpi_va_start va_start
#define uacpi_va_end va_end
#define uacpi_va_arg va_arg
#define uacpi_offsetof offsetof
#define UACPI_PRIu64 "llu"
#define UACPI_PRIx64 "llx"
#define UACPI_PRIX64 "llX"
#define UACPI_FMT64(v) ((unsigned long long)(v))
