// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// Processes and the scheduler.
//
// Each process owns an address space and a kernel stack. A process enters
// the kernel through an interrupt (syscall, IRQ or fault); the CPU switches
// to its kernel stack via tss.rsp0, and if the kernel decides to run someone
// else, schedule() parks it in switch_context() right there, mid-interrupt.
// When it is picked again, switch_context returns, the interrupt handler
// finishes, and iretq takes it back to wherever it was.
//
// All of this runs with interrupts off: every kernel entry point is an
// interrupt gate, so the scheduler's data is never touched concurrently.
// The one exception is the kernel task (PID 0), which idles with interrupts
// on -- so code it runs that touches this state brackets itself with
// irq_save()/irq_restore().

#include <types.h>
#include <proc.h>
#include <console.h>
#include <file.h>
#include <signal.h>
#include <errno.h>
#include <idt.h>
#include <gdt.h>
#include <memory.h>
#include <memset.h>
#include <memcpy.h>
#include <klog.h>
#include <serial.h>
#include <pit.h>
#include <io.h>
#include <msr.h>
#include <primemeridian.h>

static process_t procs[MAX_PROCS];
static process_t* current;
static i32 next_pid = 1;
static u32 slice = 0;

// FPU state of a freshly initialised CPU, copied into every new process.
static u8 fpu_template[512] __attribute__((aligned(16)));

void sched_init(void) {
    memset(procs, 0, sizeof(procs));

    __asm__ volatile("fninit; fxsave %0" : "=m"(fpu_template));

    process_t* k = &procs[0];
    k->pid = 0;
    k->ppid = 0;
    k->pgid = 0;
    k->state = PROC_RUNNING;
    k->space = vmm_kernel_space();
    k->kstack = NULL;                // keeps running on the boot stack
    k->cwd[0] = '/';
    memcpy(k->fpu, fpu_template, sizeof(fpu_template));
    const char* name = "kernel";
    for (int i = 0; name[i]; i++) k->name[i] = name[i];
    current = k;
    klog(LOG_OKAY, "Scheduler ready: %u process slots, %u-tick quantum\n",
         MAX_PROCS, SCHED_QUANTUM);
}

process_t* proc_current(void) {
    return current;
}

process_t* proc_slot(u32 i) {
    return &procs[i];
}

process_t* proc_by_pid(i32 pid) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (procs[i].state != PROC_UNUSED && procs[i].pid == pid) return &procs[i];
    }
    return NULL;
}

process_t* proc_alloc(const char* name, process_t* parent) {
    if (!parent) parent = &procs[0];

    process_t* p = NULL;
    for (int i = 1; i < MAX_PROCS; i++) {
        if (procs[i].state == PROC_UNUSED) { p = &procs[i]; break; }
    }
    if (!p) {
        kserialout("proc: process table full\n");
        return NULL;
    }

    u8* kstack = kmalloc(KSTACK_SIZE);
    if (!kstack) {
        kserialout("proc: out of memory for a kernel stack\n");
        return NULL;
    }
    u64 space = vmm_create_space();
    if (!space) {
        kfree(kstack);
        kserialout("proc: out of memory for an address space\n");
        return NULL;
    }

    memset(p, 0, sizeof(*p));
    p->pid = next_pid++;
    p->ppid = parent->pid;
    // Children of the kernel task start their own process group; everyone
    // else joins their parent's until they ask otherwise.
    p->pgid = parent->pid == 0 ? p->pid : parent->pgid;
    p->sid = parent->pid == 0 ? p->pid : parent->sid;
    p->ctty = parent->ctty;
    p->uid = parent->uid; p->euid = parent->euid; p->suid = parent->suid;
    p->gid = parent->gid; p->egid = parent->egid; p->sgid = parent->sgid;
    for (u32 g = 0; g < 16; g++) p->groups[g] = parent->groups[g];
    p->ngroups = parent->ngroups;
    p->umask = parent->pid == 0 ? 022 : parent->umask;
    p->state = PROC_EMBRYO;
    p->start_tick = ticks;
    p->space = space;
    p->kstack = kstack;
    p->kstack_top = ((u64)kstack + KSTACK_SIZE) & ~0xFULL;
    memcpy(p->fpu, fpu_template, sizeof(fpu_template));
    memcpy(p->cwd, parent->cwd, sizeof(p->cwd));
    fd_inherit(p, parent);
    // Ignored signals stay ignored across spawn/exec; handlers do not, since
    // the code they point at is not in the new image.
    for (int s = 0; s < NSIG; s++) {
        p->sig_handler[s] = parent->sig_handler[s] == SIG_IGN ? SIG_IGN : SIG_DFL;
    }
    p->sig_mask = parent->sig_mask;
    int i = 0;
    for (; name[i] && i < PROC_NAME_LEN - 1; i++) p->name[i] = name[i];
    p->name[i] = '\0';
    return p;
}

void proc_start_frame(process_t* p, const registers_t* frame) {
    // The interrupt frame proc_trampoline will iretq through.
    registers_t* regs = (registers_t*)(p->kstack_top - sizeof(registers_t));
    *regs = *frame;
    regs->cs = GDT_USER_CS_RPL3;
    regs->ss = GDT_USER_DS_RPL3;
    regs->eflags = (regs->eflags & 0x0CD5) | 0x202;          // IF on, IOPL 0

    // Below it, what switch_context expects to pop: r15..rbp, then `ret`.
    u64* sp = (u64*)regs;
    *--sp = (u64)proc_trampoline;
    for (int i = 0; i < 6; i++) *--sp = 0;
    p->saved_rsp = (u64)sp;

    u64 flags = irq_save();
    p->state = PROC_READY;
    irq_restore(flags);
}

void proc_start(process_t* p, u64 entry, u64 user_sp, u64 argc, u64 argv, u64 envp) {
    registers_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.rip = entry;
    frame.rsp = user_sp;
    frame.eflags = 0x202;
    frame.rdi = argc;                // main(argc, argv, envp), via crt0
    frame.rsi = argv;
    frame.rdx = envp;
    proc_start_frame(p, &frame);
}

void proc_free(process_t* p) {
    console_process_gone(p->pid);   // it may have had the screen (X)
    fd_close_all(p);
    if (p->space && p->space != vmm_kernel_space()) {
        vmm_destroy_space(p->space);
    }
    if (p->kstack) kfree(p->kstack);
    memset(p, 0, sizeof(*p));       // state = PROC_UNUSED
}

process_t* proc_fork(const registers_t* frame) {
    process_t* parent = current;
    process_t* child = proc_alloc(parent->name, parent);
    if (!child) return NULL;

    if (!vmm_clone_user(parent->space, child->space, &child->user_pages)) {
        proc_free(child);
        return NULL;
    }
    child->pgid = parent->pgid;
    child->brk_base = parent->brk_base;
    child->brk = parent->brk;
    for (u32 i = 0; i < parent->nvmas; i++) child->vmas[i] = parent->vmas[i];
    child->nvmas = parent->nvmas;
    // A forked child runs the same code, so it keeps the handlers too.
    for (int s = 0; s < NSIG; s++) child->sig_handler[s] = parent->sig_handler[s];
    child->sig_restorer = parent->sig_restorer;
    child->fs_base = parent->fs_base;
    for (u32 i = 0; i < sizeof(child->exe); i++) child->exe[i] = parent->exe[i];
    child->arg_start = parent->arg_start;
    child->arg_end = parent->arg_end;
    child->sig_linux = parent->sig_linux;
    for (int s = 0; s < NSIG; s++) {
        child->sig_flags[s] = parent->sig_flags[s];
        child->sig_actmask[s] = parent->sig_actmask[s];
        child->sig_restorers[s] = parent->sig_restorers[s];
    }
    // The parent's FPU registers are live in the CPU, not in its save area.
    __asm__ volatile("fxsave %0" : "=m"(child->fpu));

    registers_t regs = *frame;
    regs.rax = 0;                    // fork() returns 0 in the child
    proc_start_frame(child, &regs);
    return child;
}

void schedule(void) {
    process_t* prev = current;
    process_t* next = NULL;

    // Round robin over user processes, starting after the current one. The
    // kernel task is only a fallback for when nobody else can run.
    int start = (int)(prev - procs);
    for (int i = 1; i <= MAX_PROCS; i++) {
        process_t* p = &procs[(start + i) % MAX_PROCS];
        if (p->pid != 0 && p->state == PROC_READY) { next = p; break; }
    }
    if (!next) {
        if (prev->state == PROC_RUNNING) return;     // keep going
        next = &procs[0];
    }
    if (next == prev) return;

    if (prev->state == PROC_RUNNING) prev->state = PROC_READY;
    next->state = PROC_RUNNING;
    current = next;
    slice = 0;

    // The kernel itself never touches x87/SSE registers, so the outgoing
    // process's state can be saved and the incoming one's loaded right here.
    __asm__ volatile("fxsave %0" : "=m"(prev->fpu));
    __asm__ volatile("fxrstor %0" :: "m"(next->fpu));

    // Traps from ring 3 must land on the incoming process's kernel stack.
    if (next->kstack) {
        tss_set_rsp0(next->kstack_top);
        pm_set_kernel_stack(next->kstack_top);
    }
    // The thread pointer: user code reads %fs-relative TLS through it.
    wrmsr(MSR_FS_BASE, next->fs_base);
    vmm_switch(next->space);
    switch_context(&prev->saved_rsp, next->saved_rsp);
    // ...and some time later, prev is picked again and resumes here.
}

void sched_block(wait_reason_t why) {
    // A signal that is already pending must not be slept through: the
    // caller sees it and returns -EINTR instead.
    if (signal_pending(current)) return;
    current->state = PROC_BLOCKED;
    current->wait = why;
    schedule();
    current->wait = WAIT_NONE;
    current->wait_obj = NULL;
}

void sched_block_on(const void* obj) {
    current->wait_obj = obj;
    sched_block(WAIT_OBJECT);
}

bool sched_block_on_until(const void* obj, u32 deadline) {
    if ((i32)(ticks - deadline) >= 0) return false;
    current->wake_tick = deadline;
    current->wake_timed = true;
    sched_block_on(obj);
    current->wake_timed = false;
    return (i32)(ticks - deadline) < 0;
}

void sched_wake_obj(const void* obj) {
    for (int i = 1; i < MAX_PROCS; i++) {
        // poll() and select() sleep on file_poll_wait: any change might be
        // the one they are waiting for.
        if (procs[i].state == PROC_BLOCKED && procs[i].wait == WAIT_OBJECT &&
            (procs[i].wait_obj == obj || procs[i].wait_obj == &file_poll_wait)) {
            procs[i].state = PROC_READY;
        }
    }
}

void sched_interrupt(process_t* p) {
    if (p->state == PROC_BLOCKED) p->state = PROC_READY;
}

// Tell a parent something happened to its child (exit or stop).
static void notify_parent(process_t* child) {
    process_t* parent = proc_by_pid(child->ppid);
    if (!parent || parent->pid == 0 || parent->state == PROC_ZOMBIE) return;
    signal_send(parent, SIGCHLD);
    if (parent->state == PROC_BLOCKED && parent->wait == WAIT_CHILD) {
        parent->state = PROC_READY;
    }
}

void proc_stop_self(i32 sig) {
    current->state = PROC_STOPPED;
    current->stop_sig = sig;
    current->stop_unreported = true;
    kserialoutf("proc: %u (%s) stopped by signal %u\n", (u32)current->pid, current->name, (u32)sig);
    // SA_NOCLDSTOP (0x1): the parent asked not to hear about stops.
    process_t* parent = proc_by_pid(current->ppid);
    if (!parent || !(parent->sig_flags[SIGCHLD] & 0x1)) notify_parent(current);
    else if (parent->state == PROC_BLOCKED && parent->wait == WAIT_CHILD) parent->state = PROC_READY;
    schedule();                      // parked until proc_continue
}

void proc_continue(process_t* p) {
    if (p->state == PROC_STOPPED) {
        p->state = PROC_READY;
        p->stop_unreported = false;
    }
}

void sched_sleep(u32 n) {
    current->wake_tick = ticks + n;
    while ((i32)(ticks - current->wake_tick) < 0) {
        sched_block(WAIT_SLEEP);
        if (signal_pending(current)) return;
    }
}

void sched_tick(void) {
    current->cpu_ticks++;
    for (int i = 1; i < MAX_PROCS; i++) {
        process_t* p = &procs[i];
        if (p->state == PROC_BLOCKED && (p->wait == WAIT_SLEEP || p->wake_timed) &&
            (i32)(ticks - p->wake_tick) >= 0) {
            p->state = PROC_READY;
        }
        if (p->alarm_set && p->state != PROC_UNUSED && p->state != PROC_ZOMBIE &&
            (i32)(ticks - p->alarm_tick) >= 0) {
            if (p->alarm_interval) p->alarm_tick += p->alarm_interval;
            else p->alarm_set = false;
            signal_send(p, SIGALRM);
        }
    }
    // The kernel task gives way immediately; everyone else gets a quantum.
    if (current->pid == 0 || ++slice >= SCHED_QUANTUM) {
        schedule();
    }
}

bool sched_user_procs(void) {
    for (int i = 1; i < MAX_PROCS; i++) {
        if (procs[i].state != PROC_UNUSED) return true;
    }
    return false;
}

void sched_reap_orphans(void) {
    u64 flags = irq_save();
    for (int i = 1; i < MAX_PROCS; i++) {
        if (procs[i].state == PROC_ZOMBIE && procs[i].ppid == 0) {
            proc_free(&procs[i]);
        }
    }
    irq_restore(flags);
}

// Both ways out of a process end here. wstatus is already in the POSIX
// wait() encoding: (code << 8) for exit(code), or the signal number.
__attribute__((noreturn)) static void do_exit(i64 wstatus) {
    __asm__ volatile("cli");
    process_t* me = current;
    if (me->pid == 0) {
        klog(LOG_ERROR, "proc: the kernel task tried to exit\n");
        for (;;) __asm__ volatile("hlt");
    }

    // Serial only: the shell reports what matters to the user.
    if (wstatus & 0x7F) {
        kserialoutf("proc: %u (%s) killed by signal %u\n",
             (u32)me->pid, me->name, (u32)(wstatus & 0x7F));
    } else {
        kserialoutf("proc: %u (%s) exited with status %u\n",
             (u32)me->pid, me->name, (u32)((wstatus >> 8) & 0xFF));
    }
    me->exit_status = wstatus;

    // Closing descriptors may wake pipe readers (EOF) and writers (EPIPE).
    fd_close_all(me);

    // The address space can go now -- but not while it is loaded. The kernel
    // stack we are standing on has to wait for whoever reaps us.
    vmm_switch(vmm_kernel_space());
    vmm_destroy_space(me->space);
    me->space = vmm_kernel_space();

    // Orphans are adopted by the kernel task, which reaps them.
    for (int i = 1; i < MAX_PROCS; i++) {
        if (procs[i].state != PROC_UNUSED && procs[i].ppid == me->pid) {
            procs[i].ppid = 0;
        }
    }

    process_t* parent = proc_by_pid(me->ppid);
    if (!parent || parent->state == PROC_ZOMBIE) me->ppid = 0;
    else notify_parent(me);

    me->state = PROC_ZOMBIE;
    schedule();                      // never comes back: nobody runs a zombie
    for (;;) __asm__ volatile("hlt");
}

void proc_exit(i64 code) {
    do_exit((code & 0xFF) << 8);
}

void proc_exit_signal(i32 sig) {
    do_exit(sig & 0x7F);
}

void proc_fpu_reset(void) {
    __asm__ volatile("fxrstor %0" :: "m"(fpu_template));
}

static bool wait_matches(const process_t* p, i32 pid) {
    if (pid > 0) return p->pid == pid;
    if (pid == -1) return true;
    if (pid == 0) return p->pgid == current->pgid;
    return p->pgid == -pid;
}

i32 proc_wait(i32 pid, i64* status, u32 options) {
    for (;;) {
        bool have_child = false;
        for (int i = 1; i < MAX_PROCS; i++) {
            process_t* p = &procs[i];
            if (p->state == PROC_UNUSED || p->ppid != current->pid) continue;
            if (!wait_matches(p, pid)) continue;
            have_child = true;
            if (p->state == PROC_ZOMBIE) {
                i32 found = p->pid;
                if (status) *status = p->exit_status;
                proc_free(p);
                return found;
            }
            if ((options & WUNTRACED) && p->state == PROC_STOPPED && p->stop_unreported) {
                p->stop_unreported = false;
                if (status) *status = ((i64)p->stop_sig << 8) | 0x7F;
                return p->pid;
            }
        }
        if (!have_child) return -ECHILD;
        if (options & WNOHANG) return 0;
        current->wait_pid = pid;
        sched_block(WAIT_CHILD);
        if (signal_pending(current)) return -EINTR;
    }
}

bool proc_info(u32 index, proc_info_t* out) {
    u32 seen = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        if (procs[i].state == PROC_UNUSED) continue;
        if (seen++ == index) {
            out->pid = procs[i].pid;
            out->ppid = procs[i].ppid;
            out->pgid = procs[i].pgid;
            out->state = (i32)procs[i].state;
            for (int j = 0; j < PROC_NAME_LEN; j++) out->name[j] = procs[i].name[j];
            return true;
        }
    }
    return false;
}
