// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>
#include <idt.h>
#include <mman.h>

#define MAX_PROCS     32
#define MAX_FDS       32
#define NSIG          32
#define KSTACK_SIZE   (16 * KB)
#define PROC_NAME_LEN 32
#define PROC_CWD_MAX  256      // == VFS_PATH_MAX

// Timer ticks (250 Hz) a process may run before the scheduler moves on.
#define SCHED_QUANTUM 5

typedef enum {
    PROC_UNUSED = 0,
    PROC_EMBRYO,     // slot claimed, still being loaded; never scheduled
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_ZOMBIE,     // exited, waiting for its parent to collect the status
    PROC_STOPPED,    // SIGSTOP/SIGTSTP; runs again on SIGCONT
} proc_state_t;

typedef enum {
    WAIT_NONE = 0,
    WAIT_CHILD,      // a child to exit (wait_pid, or any child if -1)
    WAIT_SLEEP,      // ticks to reach wake_tick
    WAIT_OBJECT,     // something keyed by wait_obj: a pipe, the tty, ...
} wait_reason_t;

struct file;

typedef struct {
    // fxsave/fxrstor image of the x87/SSE registers. Must be 16-byte
    // aligned, which also aligns every process_t in the table.
    u8 fpu[512] __attribute__((aligned(16)));

    i32 pid;
    i32 ppid;
    i32 pgid;         // process group, for job control and terminal signals
    i32 sid;          // session
    struct tty* ctty; // controlling terminal (of the session), or NULL
    u32 umask;
    proc_state_t state;
    wait_reason_t wait;
    i32 wait_pid;
    const void* wait_obj;
    u32 wake_tick;
    bool wake_timed;  // blocked on an object, but only until wake_tick
    bool alarm_set;   // SIGALRM due at alarm_tick, then every alarm_interval
    u32 alarm_tick;
    u32 alarm_interval;
    i64 exit_status;  // POSIX wait() encoding, once a zombie
    i32 stop_sig;     // signal that stopped it, while PROC_STOPPED
    bool stop_unreported;  // waitpid(WUNTRACED) has not seen this stop yet

    u64 space;        // physical address of this process's PML4
    u32 user_pages;   // frames mapped into the lower half, capped

    u8* kstack;       // kmalloc'd kernel stack, NULL for the kernel task
    u64 kstack_top;
    u64 saved_rsp;    // valid while not running: where switch_context parked

    u64 brk_base;     // first byte past the loaded image
    u64 brk;          // current program break (sbrk)

    u64 fs_base;      // FS segment base (thread pointer), set by arch_prctl
    u64 arg_start;    // argv strings in user memory, for /proc/<pid>/cmdline
    u64 arg_end;
    u32 start_tick;   // when it was created
    u32 cpu_ticks;    // timer ticks spent running it

    vma_t vmas[MAX_VMAS];    // mmap regions, sorted by address
    u32 nvmas;

    char cwd[PROC_CWD_MAX];
    struct file* fds[MAX_FDS];
    u32 fd_cloexec;   // bit n set: close fd n on execve

    u32 sig_pending;  // bit n set: signal n waiting for delivery
    i64 syscall_nr;   // number of the syscall in progress, for restarts
    u32 sig_mask;     // bit n set: signal n blocked
    u32 sig_saved_mask;      // sigsuspend/ppoll: the mask to put back...
    bool sig_mask_saved;     // ...once the call (and any handler) is done
    u64 sig_handler[NSIG];   // SIG_DFL, SIG_IGN, or a ring 3 address
    u64 sig_restorer;        // ring 3 trampoline that calls sigreturn
    // Per-handler details for handlers installed through Prime Meridian
    // (rt_sigaction): they get a Linux signal frame instead of ours.
    u32 sig_linux;           // bit n: handler n is a Linux one
    u32 sig_flags[NSIG];     // its SA_* flags (Linux values)
    u32 sig_actmask[NSIG];   // also blocked while it runs (our bit layout)
    u64 sig_restorers[NSIG]; // its sa_restorer

    char name[PROC_NAME_LEN];
    char exe[PROC_CWD_MAX];  // the program it runs, for /proc/<pid>/exe
} process_t;

// What `ps` gets for each process.
typedef struct {
    i32 pid;
    i32 ppid;
    i32 state;        // proc_state_t
    char name[PROC_NAME_LEN];
    i32 pgid;
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
// The child inherits the parent's cwd, fds, process group and dispositions.
process_t* proc_alloc(const char* name, process_t* parent);
// Make the process runnable, entering ring 3 with exactly these registers.
void proc_start_frame(process_t* p, const registers_t* frame);
// Convenience: a fresh frame at entry with rsp, rdi, rsi, rdx set.
void proc_start(process_t* p, u64 entry, u64 user_sp, u64 argc, u64 argv, u64 envp);
// Give back everything a non-running process holds and clear its slot.
void proc_free(process_t* p);

// Duplicate the current process (fork). Returns the child, or NULL.
process_t* proc_fork(const registers_t* frame);

// Terminate the current process with exit(code), or as if killed by a
// signal. Never return.
__attribute__((noreturn)) void proc_exit(i64 code);
__attribute__((noreturn)) void proc_exit_signal(i32 sig);

// Load a clean x87/SSE state into the CPU (execve).
void proc_fpu_reset(void);

// Wait for a child (pid, or any child if -1; -pgid for a group). Returns its
// pid and stores its wait status ((code << 8) or signal number, or
// (sig << 8) | 0x7F for a stop under WUNTRACED); 0 with WNOHANG if none is
// ready yet; or -ECHILD / -EINTR.
#define WNOHANG   1
#define WUNTRACED 2      // also report children that stopped
i32 proc_wait(i32 pid, i64* status, u32 options);

// Pick the next ready process and switch to it. Call with interrupts off.
void schedule(void);
void sched_block(wait_reason_t why);
void sched_block_on(const void* obj);   // until sched_wake_obj(obj)
// The same, but give up at tick `deadline` (absolute). True if woken
// before then (by the object or a signal), false on timeout.
bool sched_block_on_until(const void* obj, u32 deadline);
void sched_wake_obj(const void* obj);
void sched_sleep(u32 ticks);
void sched_tick(void);             // from the timer IRQ
// Make a blocked process runnable early (e.g. a signal arrived).
void sched_interrupt(process_t* p);

// Job control: stop the current process (notifying its parent), or let a
// stopped one run again.
void proc_stop_self(i32 sig);
void proc_continue(process_t* p);

bool sched_user_procs(void);       // any process besides the kernel task?
void sched_reap_orphans(void);     // kernel task collects parentless zombies

// Fill `out` with the index-th live process. False past the end.
bool proc_info(u32 index, proc_info_t* out);

// Iterate the process table (for signals to whole groups).
process_t* proc_slot(u32 i);
