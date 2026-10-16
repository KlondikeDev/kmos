# This file has been generated in whole or in part by a Large Language Model

# Copyright (c) 2026 Joseph Jones
# Licensed under the Kunix License v1.0 or later
# See LICENSE for details

# The `syscall` instruction's landing point: Prime Meridian's front door.
#
# syscall leaves rip in rcx and rflags in r11, loads the kernel CS/SS from
# STAR, and masks IF (FMASK) -- but does not touch rsp. So the first job is
# to get onto this process's kernel stack. With one CPU and interrupts off,
# two globals are enough: the user rsp is parked in pm_user_rsp for exactly
# as long as it takes to push it.
#
# We then build the same registers_t an interrupt would have produced, so
# the rest of the kernel (signals, execve, fork, the scheduler) treats both
# kinds of entry alike, and leave through iretq rather than sysret. iretq is
# a little slower, but restores every register exactly as the frame says --
# whatever execve or a signal handler did to it -- and has no sysret-style
# trap for a non-canonical return address.

.section .bss
.align 8
.globl pm_kernel_rsp
pm_kernel_rsp: .quad 0
pm_user_rsp:   .quad 0

.section .text
.globl pm_syscall_entry
pm_syscall_entry:
    movq %rsp, pm_user_rsp(%rip)
    movq pm_kernel_rsp(%rip), %rsp

    pushq $0x3B                     # ss: GDT_USER_DS | 3
    pushq pm_user_rsp(%rip)         # rsp
    pushq %r11                      # rflags
    pushq $0x43                     # cs: GDT_USER_CS | 3
    pushq %rcx                      # rip (the instruction after syscall)
    pushq $0                        # err_code
    pushq $0x100                    # int_no: PM_SYSCALL_VECTOR, a marker

    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rdi
    pushq %rsi
    pushq %rbp
    pushq %rbx
    pushq %rdx
    pushq %rcx
    pushq %rax

    movq %rsp, %rdi
    call pm_syscall
    # (fork's child never comes back this way: it starts in
    # proc_trampoline, which unwinds an identical frame.)

    popq %rax
    popq %rcx
    popq %rdx
    popq %rbx
    popq %rbp
    popq %rsi
    popq %rdi
    popq %r8
    popq %r9
    popq %r10
    popq %r11
    popq %r12
    popq %r13
    popq %r14
    popq %r15
    addq $16, %rsp                  # int_no, err_code
    iretq
