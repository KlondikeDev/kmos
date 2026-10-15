# This file has been generated in whole or in part by a Large Language Model

# Copyright (c) 2026 Joseph Jones
# Licensed under the Kunix License v1.0 or later
# See LICENSE for details

.section .text

# void switch_context(uint64_t* save_rsp /* rdi */, uint64_t load_rsp /* rsi */)
#
# Every process that is not running is parked inside this function, on its
# own kernel stack. Switching is just: push the callee-saved registers (the
# C caller already saved everything else), stash rsp, load the other
# process's rsp, and unwind its pushes. The `ret` then returns into whatever
# called switch_context in *that* process -- usually schedule().
.globl switch_context
switch_context:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, (%rdi)

    movq %rsi, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret

# A new process has never called switch_context, so proc_start() forges a
# stack for it: six zeroed callee-saved slots, then the address of this
# trampoline as the return address, then a registers_t describing the ring 3
# entry point. The first switch "returns" here, and we leave exactly the way
# isr_common_stub does -- pop the registers, drop int_no/err_code, iretq.
.globl proc_trampoline
proc_trampoline:
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
    addq $16, %rsp          # int_no, err_code
    iretq
