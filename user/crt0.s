# This file has been generated in whole or in part by a Large Language Model

# Copyright (c) 2026 Joseph Jones
# Licensed under the Kunix License v1.0 or later
# See LICENSE for details

# Process entry point for ring 3 Kunix programs. The kernel drops us here with
# rsp 16-byte aligned, rdi = argc, rsi = argv, rdx = envp.

.section .text
.globl _start
_start:
    xorl %ebp, %ebp         # terminate the frame-pointer chain for debuggers
    call __libk_start       # (argc, argv, envp): sets environ, calls main, exits
1:  jmp 1b

# Signal handlers return here (the kernel pushes this address before
# entering a handler); it asks the kernel to restore the interrupted state.
.globl __libk_sigrestorer
__libk_sigrestorer:
    movq $27, %rax          # SYS_SIGRETURN
    int $0x80
2:  jmp 2b
