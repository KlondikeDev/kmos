# This file has been generated in whole or in part by a Large Language Model

# Copyright (c) 2026 Joseph Jones
# Licensed under the Kunix License v1.0 or later
# See LICENSE for details

.section .text

# ---------------------------------------------------------------------------
# Ring 3 payloads, started as processes by the kernel shell's `usermode`
# command. Each is copied verbatim into a user page, so they may only
# use immediates and RIP-relative addressing -- no absolute kernel symbols.
# ---------------------------------------------------------------------------

# Well-behaved: prints through the kernel, asks its privilege level, exits.
.globl user_program_start
.globl user_program_end
user_program_start:
    movq $1, %rax                       # SYS_WRITE
    movq $1, %rdi                       # stdout
    leaq user_msg(%rip), %rsi
    movq $(user_msg_end - user_msg), %rdx
    int $0x80

    movq $2, %rax                       # SYS_GETCPL
    int $0x80

    movq $0, %rax                       # SYS_EXIT
    movq $0, %rdi
    int $0x80
1:  jmp 1b                              # exit never returns, but just in case
user_msg:
    .ascii "Hello from ring 3! This line was printed by a syscall.\n"
user_msg_end:
user_program_end:

# Builds its output on the stack instead of printing a constant. The template
# is copied out of the read-only code page into stack memory and patched there,
# so a successful print proves sys_write accepts stack buffers.
.globl user_stack_program_start
.globl user_stack_program_end
user_stack_program_start:
    subq $64, %rsp                      # scratch buffer on the ring 3 stack
    leaq stack_tmpl(%rip), %rsi
    movq %rsp, %rdi
    movq $(stack_tmpl_end - stack_tmpl), %rcx
1:  movb (%rsi), %al
    movb %al, (%rdi)
    incq %rsi
    incq %rdi
    decq %rcx
    jnz 1b

    # Lowercase the first character in place, to prove the copy is writable.
    movb $0x74, (%rsp)                  # 'T' -> 't'

    movq $1, %rax                       # SYS_WRITE
    movq $1, %rdi                       # stdout
    movq %rsp, %rsi                     # ...from the stack, not the code page
    movq $(stack_tmpl_end - stack_tmpl), %rdx
    int $0x80

    movq %rax, %rdi                     # exit with the byte count
    movq $0, %rax                       # SYS_EXIT
    int $0x80
1:  jmp 1b
stack_tmpl:
    .ascii "This string was assembled on the ring 3 stack.\n"
stack_tmpl_end:
user_stack_program_end:

# Misbehaving: executes a privileged instruction. The CPU should raise #GP and
# the kernel should reclaim control instead of dying.
.globl user_fault_program_start
.globl user_fault_program_end
user_fault_program_start:
    movq $1, %rax                       # SYS_WRITE
    movq $1, %rdi
    leaq fault_msg(%rip), %rsi
    movq $(fault_msg_end - fault_msg), %rdx
    int $0x80

    cli                                 # privileged: must fault in ring 3
1:  jmp 1b
fault_msg:
    .ascii "Ring 3 is about to attempt a privileged instruction (cli)...\n"
fault_msg_end:
user_fault_program_end:
