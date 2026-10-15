// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// Signals.
//
// Sending only sets a bit. Delivery happens on the way out of the kernel to
// ring 3 (end of a syscall, IRQ or fault taken from user mode): a default
// action is applied there, or the interrupted register frame is saved onto
// the user stack and the frame is rewritten so iretq "returns" into the
// handler. The handler returns into the process's restorer trampoline,
// which calls SYS_SIGRETURN; that copies the saved frame back.
//
//   user stack after delivery (grows down):
//     ...interrupted code's stack...
//     [128-byte red zone, left alone]
//     saved registers_t                <- signal_return reads it from here
//     return address = sig_restorer    <- rsp when the handler starts

#include <types.h>
#include <signal.h>
#include <proc.h>
#include <usermode.h>
#include <gdt.h>
#include <memcpy.h>
#include <klog.h>
#include <errno.h>

#define RED_ZONE 128

// What happens to a signal nobody installed a handler for.
static bool default_is_ignore(i32 sig) {
    return sig == SIGCHLD || sig == SIGWINCH || sig == SIGCONT;
}

static bool default_is_stop(i32 sig) {
    return sig == SIGSTOP || sig == SIGTSTP || sig == SIGTTIN || sig == SIGTTOU;
}

void signal_send(process_t* p, i32 sig) {
    if (sig <= 0 || sig >= NSIG || !p || p->state == PROC_ZOMBIE) return;

    // Job control acts at send time, whatever the disposition: SIGCONT
    // resumes a stopped process and cancels any stop still pending; a stop
    // cancels a pending SIGCONT; SIGKILL must reach even a stopped process.
    u32 stops = (1u << SIGSTOP) | (1u << SIGTSTP) | (1u << SIGTTIN) | (1u << SIGTTOU);
    if (sig == SIGCONT) {
        p->sig_pending &= ~stops;
        proc_continue(p);
    } else if (stops & (1u << sig)) {
        p->sig_pending &= ~(1u << SIGCONT);
    }
    if (sig == SIGKILL) proc_continue(p);

    // An ignored signal is discarded at once (POSIX), so it can never
    // interrupt a blocking call.
    if (sig != SIGKILL && sig != SIGSTOP) {
        u64 h = p->sig_handler[sig];
        if (h == SIG_IGN || (h == SIG_DFL && default_is_ignore(sig))) return;
    }
    p->sig_pending |= 1u << sig;
    if (sig == SIGKILL || !(p->sig_mask & (1u << sig))) sched_interrupt(p);
}

i32 signal_send_group(i32 pgid, i32 sig) {
    i32 n = 0;
    for (u32 i = 1; i < MAX_PROCS; i++) {
        process_t* p = proc_slot(i);
        if (p->state != PROC_UNUSED && p->state != PROC_ZOMBIE && p->pgid == pgid) {
            signal_send(p, sig);
            n++;
        }
    }
    return n;
}

static u32 deliverable(const process_t* p) {
    // SIGKILL and SIGSTOP cannot be blocked.
    u32 unmaskable = (1u << SIGKILL) | (1u << SIGSTOP);
    return p->sig_pending & (~p->sig_mask | unmaskable);
}

bool signal_pending(const process_t* p) {
    return deliverable(p) != 0;
}

void signal_deliver(registers_t* regs) {
    signal_deliver_from(regs, false);
}

void signal_deliver_from(registers_t* regs, bool from_syscall) {
    process_t* p = proc_current();
    if (p->pid == 0) return;
    bool handler_ran = false;

    u32 ready;
    while ((ready = deliverable(p)) != 0) {
        i32 sig = __builtin_ctz(ready);
        p->sig_pending &= ~(1u << sig);

        u64 handler = (sig == SIGKILL || sig == SIGSTOP) ? SIG_DFL : p->sig_handler[sig];
        if (handler == SIG_IGN) continue;
        if (handler == SIG_DFL) {
            if (default_is_ignore(sig)) continue;
            if (default_is_stop(sig)) {
                proc_stop_self(sig);         // returns once continued
                continue;
            }
            proc_exit_signal(sig);                           // does not return
        }

        // Build the handler frame. If the stack is unusable the process
        // cannot take the signal at all, which is fatal -- as on Unix.
        u64 sp = regs->rsp - RED_ZONE - sizeof(registers_t);
        sp &= ~0xFULL;
        u64 frame = sp;
        sp -= 8;                                             // return address
        if (!p->sig_restorer || !user_range_ok(sp, (frame - sp) + sizeof(registers_t), true)) {
            klog(LOG_WARN, "signal: pid %u cannot take signal %u, killing it\n",
                 (u32)p->pid, (u32)sig);
            proc_exit_signal(SIGSEGV);
        }
        memcpy((void*)frame, regs, sizeof(registers_t));
        *(u64*)sp = p->sig_restorer;

        regs->rsp = sp;
        regs->rip = handler;
        regs->rdi = (u64)sig;
        handler_ran = true;
        break;                                               // one at a time
    }

    // A syscall that was only interrupted so the process could be stopped
    // (or by an ignored signal) is restarted transparently, as with BSD's
    // and Linux's SA_RESTART: back rip up over the 2-byte `int $0x80` and
    // put the syscall number back in rax. A handler that ran sees EINTR.
    if (from_syscall && !handler_ran && p->syscall_nr >= 0 && (i64)regs->rax == -EINTR) {
        regs->rip -= 2;
        regs->rax = (u64)p->syscall_nr;
    }
}

void signal_return(registers_t* regs) {
    // The handler's `ret` popped the restorer address, so rsp now points at
    // the saved frame.
    u64 frame = regs->rsp;
    if (!user_range_ok(frame, sizeof(registers_t), false)) {
        proc_exit_signal(SIGSEGV);
    }
    registers_t saved;
    memcpy(&saved, (const void*)frame, sizeof(saved));

    // The frame lived in user memory, so ring 3 could have edited it. Only
    // take what is safe: never a kernel selector, never IOPL, always IF --
    // and never a RIP outside the user half: iretq to a non-canonical
    // address faults in ring 0, which would take the whole kernel down.
    if (saved.rip >= USER_STACK_TOP || saved.rsp > USER_STACK_TOP) {
        proc_exit_signal(SIGSEGV);
    }
    saved.cs = GDT_USER_CS_RPL3;
    saved.ss = GDT_USER_DS_RPL3;
    saved.eflags = (saved.eflags & 0x0CD5) | 0x202;
    *regs = saved;
}

i32 signal_for_fault(u64 vector) {
    switch (vector) {
        case 0:  return SIGFPE;          // divide error
        case 16: return SIGFPE;          // x87
        case 19: return SIGFPE;          // SIMD
        case 6:  return SIGILL;          // invalid opcode
        case 3:  return SIGTRAP;
        case 17: return SIGBUS;          // alignment check
        default: return SIGSEGV;         // #GP, #PF, #SS, ...
    }
}
