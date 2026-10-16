// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// Linux signal frames.
//
// A handler installed with rt_sigaction is entered the way Linux enters
// one: rdi = signal, rsi = &siginfo, rdx = &ucontext, and the stack holds
// an rt_sigframe whose first word -- the handler's return address -- is the
// sa_restorer libc supplied, which calls rt_sigreturn. The interrupted
// registers, signal mask and FPU/SSE state all travel in the frame, so a
// handler may use SSE freely, and sigreturn (or a siglongjmp) restores
// exactly what was interrupted.
//
//   user stack after delivery (grows down):
//     ...interrupted code's stack...
//     [128-byte red zone, left alone]
//     fxsave image (512 bytes, 64-aligned)   <- uc_mcontext.fpstate
//     l_rt_sigframe_t { pretcode, ucontext, siginfo }
//                                     <- rsp at handler entry: pretcode

#include <types.h>
#include <primemeridian.h>
#include <proc.h>
#include <signal.h>
#include <usermode.h>
#include <gdt.h>
#include <memcpy.h>
#include <memset.h>
#include <klog.h>
#include <errno.h>
#include "linux.h"

#define RED_ZONE 128

static u8 fx_buf[512] __attribute__((aligned(16)));

// Calls Linux never restarts once a handler has run, SA_RESTART or not
// (they return ERESTARTNOHAND or use a restart block): waiting for a
// signal is their whole job, or the caller must see the time that passed.
static bool restartable(i64 nr) {
    switch (nr) {
        case LNX_pause: case LNX_rt_sigsuspend: case LNX_rt_sigtimedwait:
        case LNX_poll: case LNX_ppoll: case LNX_select: case LNX_pselect6:
        case LNX_nanosleep: case LNX_clock_nanosleep: case LNX_futex:
            return false;
        default:
            return true;
    }
}

bool pm_signal_setup(process_t* p, registers_t* regs, i32 sig, u64 handler, bool from_syscall) {
    u32 flags = p->sig_flags[sig];
    u64 restorer = p->sig_restorers[sig];
    if (!(flags & L_SA_RESTORER) || !restorer) return false;  // nowhere to return to

    // SA_RESTART: a syscall this interrupted runs again after the handler
    // instead of failing with EINTR. Rewind it before the frame is saved.
    if (from_syscall && (flags & L_SA_RESTART) && p->syscall_nr >= 0 && (i64)regs->rax == -EINTR &&
        restartable(p->syscall_nr)) {
        regs->rip -= 2;                                // back over `syscall` (0F 05)
        regs->rax = (u64)p->syscall_nr;
    }

    u64 sp = regs->rsp - RED_ZONE;
    u64 fp = (sp - sizeof(fx_buf)) & ~63ULL;
    u64 frame = ((fp - sizeof(l_rt_sigframe_t)) & ~15ULL) - 8;   // rsp+8 16-aligned, as after a call
    if (!user_range_ok(frame, fp + sizeof(fx_buf) - frame, true)) return false;

    // The FPU/SSE registers are still the interrupted code's: the kernel
    // never touches them.
    __asm__ volatile("fxsave %0" : "=m"(fx_buf));
    memcpy((void*)fp, fx_buf, sizeof(fx_buf));

    l_rt_sigframe_t* f = (l_rt_sigframe_t*)frame;
    memset(f, 0, sizeof(*f));
    f->pretcode = restorer;

    l_sigcontext_t* mc = &f->uc.uc_mcontext;
    mc->r8 = regs->r8;   mc->r9 = regs->r9;   mc->r10 = regs->r10; mc->r11 = regs->r11;
    mc->r12 = regs->r12; mc->r13 = regs->r13; mc->r14 = regs->r14; mc->r15 = regs->r15;
    mc->rdi = regs->rdi; mc->rsi = regs->rsi; mc->rbp = regs->rbp; mc->rbx = regs->rbx;
    mc->rdx = regs->rdx; mc->rax = regs->rax; mc->rcx = regs->rcx; mc->rsp = regs->rsp;
    mc->rip = regs->rip; mc->eflags = regs->eflags;
    mc->cs = (u16)regs->cs;
    mc->ss = (u16)regs->ss;
    mc->trapno = regs->int_no < 32 ? regs->int_no : 0;
    mc->err = regs->int_no < 32 ? regs->err_code : 0;
    mc->fpstate = fp;

    // The mask to restore afterwards: the caller's own, even if a
    // sigsuspend had a temporary one in place.
    u32 old_mask = p->sig_mask;
    if (p->sig_mask_saved) {
        old_mask = p->sig_saved_mask;
        p->sig_mask_saved = false;
    }
    f->uc.uc_sigmask = l_mask_to_sigset(old_mask);
    mc->oldmask = f->uc.uc_sigmask;
    f->uc.uc_stack.ss_flags = L_SS_DISABLE;

    f->info.si_signo = sig;
    f->info.si_code = 0;                               // SI_USER
    if (regs->int_no == 14) {
        u64 cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        f->info.u.fault.addr = cr2;
        mc->cr2 = cr2;
        f->info.si_code = 1;                           // SEGV_MAPERR
    }

    // Block the signal itself (unless SA_NODEFER) and sa_mask while the
    // handler runs; SA_RESETHAND makes this delivery the last.
    p->sig_mask |= p->sig_actmask[sig];
    if (!(flags & L_SA_NODEFER)) p->sig_mask |= 1u << sig;
    p->sig_mask &= ~((1u << SIGKILL) | (1u << SIGSTOP));
    if (flags & L_SA_RESETHAND) {
        p->sig_handler[sig] = SIG_DFL;
        p->sig_linux &= ~(1u << sig);
    }

    regs->rsp = frame;
    regs->rip = handler;
    regs->rdi = (u64)sig;
    regs->rsi = frame + __builtin_offsetof(l_rt_sigframe_t, info);
    regs->rdx = frame + __builtin_offsetof(l_rt_sigframe_t, uc);
    regs->rax = 0;
    regs->eflags &= ~0x500ULL;                         // DF and TF clear for the handler
    proc_fpu_reset();                                  // a clean FPU, as Linux gives
    return true;
}

void pm_sigreturn(registers_t* regs) {
    process_t* p = proc_current();
    // The handler's `ret` popped pretcode, so rsp is at the ucontext.
    u64 uc_addr = regs->rsp;
    if (!user_range_ok(uc_addr, sizeof(l_ucontext_t), false)) {
        klog(LOG_WARN, "primemeridian: pid %u: bad rt_sigreturn frame\n", (u32)p->pid);
        proc_exit_signal(SIGSEGV);
    }
    l_ucontext_t uc;
    memcpy(&uc, (const void*)uc_addr, sizeof(uc));
    const l_sigcontext_t* mc = &uc.uc_mcontext;

    // The frame lived in user memory, so ring 3 could have edited it. Only
    // take what is safe: user selectors, no IOPL, IF on, a user-half rip.
    if (mc->rip >= USER_STACK_TOP || mc->rsp > USER_STACK_TOP) {
        proc_exit_signal(SIGSEGV);
    }
    if (mc->fpstate) {
        if (!user_range_ok(mc->fpstate, sizeof(fx_buf), false)) proc_exit_signal(SIGSEGV);
        memcpy(fx_buf, (const void*)mc->fpstate, sizeof(fx_buf));
        // MXCSR bits the CPU does not have would make fxrstor fault.
        *(u32*)(fx_buf + 24) &= 0xFFBF;
        __asm__ volatile("fxrstor %0" :: "m"(fx_buf));
    } else {
        proc_fpu_reset();
    }

    regs->r8 = mc->r8;   regs->r9 = mc->r9;   regs->r10 = mc->r10; regs->r11 = mc->r11;
    regs->r12 = mc->r12; regs->r13 = mc->r13; regs->r14 = mc->r14; regs->r15 = mc->r15;
    regs->rdi = mc->rdi; regs->rsi = mc->rsi; regs->rbp = mc->rbp; regs->rbx = mc->rbx;
    regs->rdx = mc->rdx; regs->rax = mc->rax; regs->rcx = mc->rcx; regs->rsp = mc->rsp;
    regs->rip = mc->rip;
    regs->eflags = (mc->eflags & 0x0CD5) | 0x202;
    regs->cs = GDT_USER_CS_RPL3;
    regs->ss = GDT_USER_DS_RPL3;

    p->sig_mask = l_sigset_to_mask(uc.uc_sigmask) & ~((1u << SIGKILL) | (1u << SIGSTOP));
}
