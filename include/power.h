// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>

// Ending the system. Each first lets every filesystem shut down cleanly
// (vfs_shutdown_all: UFS2 marks itself clean), then stops the machine.
// None of them return.
#define POWER_HALT     0
#define POWER_OFF      1
#define POWER_RESTART  2
__attribute__((noreturn)) void power_down(u32 how);

// The machine's name (sethostname, uname's nodename).
extern char kernel_hostname[65];
