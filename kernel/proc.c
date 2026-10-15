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
#include <idt.h>
#include <gdt.h>
#include <memory.h>
#include <memset.h>
#include <klog.h>
#include <pit.h>
#include <io.h>

static process_t procs[MAX_PROCS];
static process_t* current;
static i32 next_pid = 1;
static u32 slice = 0;

void sched_init(void) {
    memset(procs, 0, sizeof(procs));
    process_t* k = &procs[0];
    k->pid = 0;
    k->ppid = 0;
    k->state = PROC_RUNNING;
    k->space = vmm_kernel_space();
    k->kstack = NULL;                // keeps running on the boot stack
    const char* name = "kernel";
    for (int i = 0; name[i]; i++) k->name[i] = name[i];
    current = k;
    klog(LOG_OKAY, "Scheduler ready: %u process slots, %u-tick quantum\n",
         MAX_PROCS, SCHED_QUANTUM);
}

process_t* proc_current(void) {
    return current;
}

process_t* proc_by_pid(i32 pid) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (procs[i].state != PROC_UNUSED && procs[i].pid == pid) return &procs[i];
    }
    return NULL;
}

process_t* proc_alloc(const char* name, i32 ppid) {
    process_t* p = NULL;
    for (int i = 1; i < MAX_PROCS; i++) {
        if (procs[i].state == PROC_UNUSED) { p = &procs[i]; break; }
    }
    if (!p) {
        klog(LOG_ERROR, "proc: process table full\n");
        return NULL;
    }

    u8* kstack = kmalloc(KSTACK_SIZE);
    if (!kstack) {
        klog(LOG_ERROR, "proc: out of memory for a kernel stack\n");
        return NULL;
    }
    u64 space = vmm_create_space();
    if (!space) {
        kfree(kstack);
        klog(LOG_ERROR, "proc: out of memory for an address space\n");
        return NULL;
    }

    memset(p, 0, sizeof(*p));
    p->pid = next_pid++;
    p->ppid = ppid;
    p->state = PROC_EMBRYO;
    p->space = space;
    p->kstack = kstack;
    p->kstack_top = ((u64)kstack + KSTACK_SIZE) & ~0xFULL;
    int i = 0;
    for (; name[i] && i < PROC_NAME_LEN - 1; i++) p->name[i] = name[i];
    p->name[i] = '\0';
    return p;
}

void proc_start(process_t* p, u64 entry, u64 user_sp, u64 argc, u64 argv) {
    // The interrupt frame proc_trampoline will iretq through.
    registers_t* regs = (registers_t*)(p->kstack_top - sizeof(registers_t));
    memset(regs, 0, sizeof(*regs));
    regs->rip = entry;
    regs->cs = GDT_USER_CS_RPL3;
    regs->eflags = 0x202;            // reserved bit 1, IF set
    regs->rsp = user_sp;
    regs->ss = GDT_USER_DS_RPL3;
    regs->rdi = argc;                // main(argc, argv), via crt0
    regs->rsi = argv;

    // Below it, what switch_context expects to pop: r15..rbp, then `ret`.
    u64* sp = (u64*)regs;
    *--sp = (u64)proc_trampoline;
    for (int i = 0; i < 6; i++) *--sp = 0;
    p->saved_rsp = (u64)sp;

    u64 flags = irq_save();
    p->state = PROC_READY;
    irq_restore(flags);
}

void proc_free(process_t* p) {
    if (p->space && p->space != vmm_kernel_space()) {
        vmm_destroy_space(p->space);
    }
    if (p->kstack) kfree(p->kstack);
    memset(p, 0, sizeof(*p));       // state = PROC_UNUSED
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

    // Traps from ring 3 must land on the incoming process's kernel stack.
    if (next->kstack) tss_set_rsp0(next->kstack_top);
    vmm_switch(next->space);
    switch_context(&prev->saved_rsp, next->saved_rsp);
    // ...and some time later, prev is picked again and resumes here.
}

void sched_block(wait_reason_t why) {
    current->state = PROC_BLOCKED;
    current->wait = why;
    schedule();
    current->wait = WAIT_NONE;
}

void sched_sleep(u32 n) {
    current->wake_tick = ticks + n;
    sched_block(WAIT_SLEEP);
}

void sched_tick(void) {
    for (int i = 1; i < MAX_PROCS; i++) {
        process_t* p = &procs[i];
        if (p->state == PROC_BLOCKED && p->wait == WAIT_SLEEP &&
            (i32)(ticks - p->wake_tick) >= 0) {
            p->state = PROC_READY;
        }
    }
    // The kernel task gives way immediately; everyone else gets a quantum.
    if (current->pid == 0 || ++slice >= SCHED_QUANTUM) {
        schedule();
    }
}

void sched_wake_input(void) {
    for (int i = 1; i < MAX_PROCS; i++) {
        if (procs[i].state == PROC_BLOCKED && procs[i].wait == WAIT_INPUT) {
            procs[i].state = PROC_READY;
        }
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

void proc_exit(i64 status) {
    __asm__ volatile("cli");
    process_t* me = current;
    if (me->pid == 0) {
        klog(LOG_ERROR, "proc: the kernel task tried to exit\n");
        for (;;) __asm__ volatile("hlt");
    }

    klog(LOG_OKAY, "proc: %u (%s) exited with status %d\n",
         (u32)me->pid, me->name, (i32)status);
    me->exit_status = status;

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
    if (parent && parent->state == PROC_BLOCKED && parent->wait == WAIT_CHILD &&
        (parent->wait_pid == -1 || parent->wait_pid == me->pid)) {
        parent->state = PROC_READY;
    }
    if (!parent || parent->state == PROC_ZOMBIE) me->ppid = 0;

    me->state = PROC_ZOMBIE;
    schedule();                      // never comes back: nobody runs a zombie
    for (;;) __asm__ volatile("hlt");
}

i32 proc_wait(i32 pid, i64* status) {
    for (;;) {
        bool have_child = false;
        for (int i = 1; i < MAX_PROCS; i++) {
            process_t* p = &procs[i];
            if (p->state == PROC_UNUSED || p->ppid != current->pid) continue;
            if (pid != -1 && p->pid != pid) continue;
            have_child = true;
            if (p->state == PROC_ZOMBIE) {
                i32 found = p->pid;
                if (status) *status = p->exit_status;
                proc_free(p);
                return found;
            }
        }
        if (!have_child) return -1;
        current->wait_pid = pid;
        sched_block(WAIT_CHILD);
    }
}

bool proc_info(u32 index, proc_info_t* out) {
    u32 seen = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        if (procs[i].state == PROC_UNUSED) continue;
        if (seen++ == index) {
            out->pid = procs[i].pid;
            out->ppid = procs[i].ppid;
            out->state = (i32)procs[i].state;
            for (int j = 0; j < PROC_NAME_LEN; j++) out->name[j] = procs[i].name[j];
            return true;
        }
    }
    return false;
}
