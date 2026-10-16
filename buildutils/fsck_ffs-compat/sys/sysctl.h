#pragma once
#include <stddef.h>
#include <errno.h>
static inline int sysctlbyname(const char* n, void* o, size_t* ol, const void* ni, size_t nl) { (void)n;(void)o;(void)ol;(void)ni;(void)nl; errno = ENOENT; return -1; }
static inline int sysctl(const int* name, unsigned namelen, void* o, size_t* ol, const void* ni, size_t nl) { (void)name;(void)namelen;(void)o;(void)ol;(void)ni;(void)nl; errno = ENOENT; return -1; }
