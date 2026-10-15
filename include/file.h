// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>
#include <vfs.h>
#include <proc.h>

// Open file descriptions and per-process descriptor tables, POSIX style.
//
// A file_t is one open(): it holds the position and the open flags, and is
// shared -- by reference count -- between every descriptor that refers to it
// (after dup, dup2, fork or spawn). So two processes that inherited the same
// descriptor also share its offset, exactly as on Unix.

// open() flags. The values are Linux's, so a Linux personality can pass
// them straight through.
#define O_RDONLY    00
#define O_WRONLY    01
#define O_RDWR      02
#define O_ACCMODE   03
#define O_CREAT     0100
#define O_EXCL      0200
#define O_TRUNC     01000
#define O_APPEND    02000
#define O_NONBLOCK  04000
#define O_DIRECTORY 0200000
#define O_CLOEXEC   02000000

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define PIPE_BUF_SIZE 4096

typedef struct file file_t;

typedef struct {
    // buf is already validated (or a kernel pointer). Return bytes or -errno.
    i64 (*read)(file_t* f, void* buf, u64 len);
    i64 (*write)(file_t* f, const void* buf, u64 len);
    void (*release)(file_t* f);        // the last reference is gone
} file_ops_t;

struct file {
    const file_ops_t* ops;
    u32 refs;
    u32 flags;                         // O_* given to open (access mode etc.)
    u64 pos;
    vnode_t node;
    char* path;                        // directories only: for readdir
    void* priv;                        // pipe state, ...
};

// Open a path. Returns 0 and the new file, or -errno.
i32 file_open(const char* path, u32 flags, file_t** out);
void file_ref(file_t* f);
void file_unref(file_t* f);

// Make a pipe: a read end and a write end.
i32 pipe_create(file_t** read_end, file_t** write_end);

// Descriptor tables. All of these act on process p.
i32 fd_install(process_t* p, file_t* f, i32 lowest);   // new fd >= lowest, or -EMFILE
file_t* fd_get(process_t* p, i32 fd);                   // NULL if not open
i32 fd_close(process_t* p, i32 fd);
void fd_close_all(process_t* p);
void fd_close_on_exec(process_t* p);
void fd_inherit(process_t* child, const process_t* parent);

// Read/write through a descriptor's file, honouring access mode and offset.
i64 file_read(file_t* f, void* buf, u64 len);
i64 file_write(file_t* f, const void* buf, u64 len);
