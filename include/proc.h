// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>

#define MAX_PROCS     32
#define KSTACK_SIZE   (16 * KB)
#define PROC_NAME_LEN 32

// Timer ticks (250 Hz) a process may run before the scheduler moves on.
#define SCHED_QUANTUM 5

typedef enum {
    PROC_UNUSED = 0,
    PROC_EMBRYO,     // slot claimed, still being loaded; never scheduled
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_ZOMBIE,     // exited, waiting for its parent to collect the status
} proc_state_t;

typedef enum {
    WAIT_NONE = 0,
    WAIT_INPUT,      // a line from the keyboard
    WAIT_CHILD,      // a child to exit (wait_pid, or any child if -1)
    WAIT_SLEEP,      // ticks to reach wake_tick
} wait_reason_t;

typedef struct {
    i32 pid;
    i32 ppid;
    proc_state_t state;
    wait_reason_t wait;
    i32 wait_pid;
    u32 wake_tick;
    i64 exit_status;

    u64 space;        // physical address of this process's PML4
    u32 user_pages;   // frames mapped into the lower half, capped

    u8* kstack;       // kmalloc'd kernel stack, NULL for the kernel task
    u64 kstack_top;
    u64 saved_rsp;    // valid while not running: where switch_context parked

    u64 brk_base;     // first byte past the loaded image
    u64 brk;          // current program break (sbrk)

    char name[PROC_NAME_LEN];
} process_t;

// What `ps` gets for each process.
typedef struct {
    i32 pid;
    i32 ppid;
    i32 state;        // proc_state_t
    char name[PROC_NAME_LEN];
} proc_info_t;

// Implemented in arch/x86/switch.s.
void switch_context(u64* save_rsp, u64 load_rsp);
void proc_trampoline(void);

// The boot thread becomes PID 0, the kernel task: it runs the kernel shell
// and idles when nothing else is ready.
void sched_init(void);

process_t* proc_current(void);
process_t* proc_by_pid(i32 pid);

// Claim a slot with a fresh address space and kernel stack, in PROC_EMBRYO.
process_t* proc_alloc(const char* name, i32 ppid);
// Forge the first ring 3 entry and make the process runnable.
void proc_start(process_t* p, u64 entry, u64 user_sp, u64 argc, u64 argv);
// Give back everything a non-running process holds and clear its slot.
void proc_free(process_t* p);

// Terminate the current process. Never returns.
__attribute__((noreturn)) void proc_exit(i64 status);

// Wait for a child (pid, or any child if -1) to exit. Returns its pid and
// stores its status, or -1 if there is no such child.
i32 proc_wait(i32 pid, i64* status);

// Pick the next ready process and switch to it. Call with interrupts off.
void schedule(void);
void sched_block(wait_reason_t why);
void sched_sleep(u32 ticks);
void sched_tick(void);             // from the timer IRQ
void sched_wake_input(void);       // from the keyboard IRQ on Enter

bool sched_user_procs(void);       // any process besides the kernel task?
void sched_reap_orphans(void);     // kernel task collects parentless zombies

// Fill `out` with the index-th live process. False past the end.
bool proc_info(u32 index, proc_info_t* out);
