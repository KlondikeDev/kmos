// This file has been generated in whole or in part by a Large Language Model
#pragma once
#include <vfs.h>

// Returns a clean, zero-padded file page with one reference for the caller,
// or -errno. The cache keeps a separate reference; callers must never write
// into a returned frame without copying it first.
i64 pagecache_get(vnode_t* node, u64 off);
i64 pagecache_read(vnode_t* node, u64 off, void* dst, u64 len);
void pagecache_forget(const vnode_t* node);
void pagecache_reclaim(void);
u64 pagecache_pages(void);
