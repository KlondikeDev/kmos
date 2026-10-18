// This file has been generated in whole or in part by a Large Language Model
#pragma once
#include <types.h>
static inline void* lwport_memset(void* d,int c,size_t n) { u8* p=d;while(n--)*p++=(u8)c;return d; }
static inline void* lwport_memcpy(void* d,const void* s,size_t n) { u8* p=d;const u8* q=s;while(n--)*p++=*q++;return d; }
static inline int lwport_memcmp(const void* a,const void* b,size_t n) { const u8*p=a,*q=b;while(n--){if(*p!=*q)return *p-*q;p++;q++;}return 0; }
static inline size_t lwport_strlen(const char* s) { size_t n=0;while(s[n])n++;return n; }
static inline int lwport_strncmp(const char*a,const char*b,size_t n) {while(n--){if(*a!=*b)return (u8)*a-(u8)*b;if(!*a)return 0;a++;b++;}return 0;}
#define memset lwport_memset
#define memcpy lwport_memcpy
#define memcmp lwport_memcmp
#define strlen lwport_strlen
#define strncmp lwport_strncmp
static inline void* lwport_memmove(void*d,const void*s,size_t n) {u8*p=d;const u8*q=s;if(p<q){while(n--)*p++=*q++;}else{while(n){n--;p[n]=q[n];}}return d;}
static inline int lwport_strcmp(const char*a,const char*b) {while(*a&&*a==*b){a++;b++;}return (u8)*a-(u8)*b;}
#define memmove lwport_memmove
#define strcmp lwport_strcmp
