// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>
#include <idt.h>
#include <proc.h>
#include <vfs.h>

// Page table entry bits, spelled out so the flag words below read clearly.
#define PTE_PRESENT 0x1
#define PTE_WRITE   0x2
#define PTE_USER    0x4
#define PTE_NX      (1ULL << 63)   // only legal once EFER.NXE is set

#define USER_PAGE_SIZE 4096ULL

// The user half of every address space. Everything lives in PML4 slot 0
// (the bottom 512 GiB); slots 256-511 are the shared kernel half.
//
//   0x0000000000400000  USER_CODE_BASE   lowest address a program may load at
//          ...           program image (ELF PT_LOAD segments)
//          ...           heap, grown upwards by sbrk
//   USER_LOAD_LIMIT      nothing (image or heap) may reach past here
//          ...           unmapped guard page
//   USER_STACK_BOTTOM    stack, USER_STACK_PAGES pages, grows down
//   USER_STACK_TOP       argv strings and pointers, then the initial rsp
#define USER_CODE_BASE    0x0000000000400000ULL
#define USER_STACK_TOP    0x0000007FFFFFF000ULL
#define USER_STACK_PAGES  4
#define USER_STACK_BOTTOM (USER_STACK_TOP - USER_STACK_PAGES * USER_PAGE_SIZE)
#define USER_LOAD_LIMIT   (USER_STACK_BOTTOM - USER_PAGE_SIZE)

// Hard caps, so a hostile or broken program cannot eat all of memory.
#define USER_MAX_PAGES    1024             // per process: 4 MiB
#define USER_MAX_IMAGE    (1 * MB)         // largest executable we will read
#define USER_MAX_ARGS     32               // each of argv and envp
#define USER_MAX_ARGBYTES 4096             // all their strings together

#define SYSCALL_VECTOR 0x80

// argv/envp gathered into kernel memory, ready to be laid out on a new stack.
typedef struct {
    u32 argc;
    const char* argv[USER_MAX_ARGS];
    u32 envc;
    const char* envp[USER_MAX_ARGS];
    char strings[USER_MAX_ARGBYTES];
} user_args_t;

extern char user_program_start[];
extern char user_program_end[];
extern char user_stack_program_start[];
extern char user_stack_program_end[];
extern char user_fault_program_start[];
extern char user_fault_program_end[];

// ---- building a process's address space (used by the ELF loader) ----

// Back `virt`'s page with a zeroed frame in p's address space, with the given
// PF_R/PF_W/PF_X rights. Mapping an already-mapped page widens its rights.
bool user_map(process_t* p, u64 virt, u32 pf_flags);
// Copy kernel bytes into p's (already mapped) memory, from any address space.
bool user_copy_to(process_t* p, u64 virt, const void* src, u64 len);

// True when every byte of [addr, addr+len) is mapped user-accessible in the
// *current* address space -- and writable, if `write` is set.
bool user_range_ok(u64 addr, u64 len, bool write);

// Copy a NUL-terminated string out of the current address space, checking
// every byte's page. False if it is unmapped or longer than max.
bool copy_string_from_user(char* dst, u64 src, u64 max);

// ---- starting programs ----

// Load an ELF from disk into a new child of `parent` (NULL = the kernel
// task). args may be NULL. Returns the new pid, or -errno.
i32 usermode_spawn(const char* path, const user_args_t* args, process_t* parent);

// execve: replace the current process's image. On success rewrites *regs
// to enter the new program and returns 0; on failure nothing has changed.
i32 usermode_exec(const char* path, const user_args_t* args, registers_t* regs);

// Load a flat, position-independent blob (one page at most) as a process.
i32 usermode_spawn_blob(const char* name, const char* start, const char* end);

// For the kernel shell: idle until the process is gone.
void usermode_run_and_wait(i32 pid);

// Hand-written payloads in arch/x86/usermode.s, run from the kernel shell.
void usermode_demo(void);
void usermode_stack_demo(void);
void usermode_fault_demo(void);

// Called from the trap handlers in kernel/idt.c.
void syscall_dispatch(registers_t* regs);
void usermode_fault(const char* reason, registers_t* regs);
