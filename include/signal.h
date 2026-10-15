// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>
#include <proc.h>
#include <idt.h>

// Signal numbers. These are Linux's (1-15 are also the same on the BSDs).
#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGILL   4
#define SIGTRAP  5
#define SIGABRT  6
#define SIGBUS   7
#define SIGFPE   8
#define SIGKILL  9
#define SIGUSR1  10
#define SIGSEGV  11
#define SIGUSR2  12
#define SIGPIPE  13
#define SIGALRM  14
#define SIGTERM  15
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19
#define SIGTSTP  20
#define SIGWINCH 28

#define SIG_DFL 0
#define SIG_IGN 1

// Mark a signal pending on p and, if p is blocked, wake it so the blocking
// call can return -EINTR.
void signal_send(process_t* p, i32 sig);
// Send to every process in a process group. Returns how many got it.
i32 signal_send_group(i32 pgid, i32 sig);

// Any unblocked signal waiting?
bool signal_pending(const process_t* p);

// On the way back to ring 3: act on pending signals -- ignore, kill the
// process, or rewrite the frame so it resumes in a handler.
void signal_deliver(registers_t* regs);

// SYS_SIGRETURN: restore the frame saved when the handler was entered.
void signal_return(registers_t* regs);

// Signal a ring 3 fault becomes (SIGSEGV for a page fault, ...).
i32 signal_for_fault(u64 vector);
