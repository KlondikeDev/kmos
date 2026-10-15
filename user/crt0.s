# This file has been generated in whole or in part by a Large Language Model

# Copyright (c) 2026 Joseph Jones
# Licensed under the Kunix License v1.0 or later
# See LICENSE for details

# Process entry point for ring 3 Kunix programs. The kernel drops us here with
# rsp at USER_STACK_TOP (16-byte aligned) and every register zeroed.

.section .text
.globl _start
_start:
    xorl %ebp, %ebp         # terminate the frame-pointer chain for debuggers
    call main               # the call leaves rsp 16n-8, as the ABI expects

    movq %rax, %rdi         # main's return value is the exit status
    movq $0, %rax           # SYS_EXIT
    int $0x80
1:  jmp 1b                  # exit never returns, but just in case
