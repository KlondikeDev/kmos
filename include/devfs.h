// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>
#include <vfs.h>

// Device nodes: /dev/tty (the console terminal), /dev/null, /dev/zero.
extern const vfs_ops_t devfs_vfs_ops;

// Is this node the terminal? (ioctl and isatty need to know.)
bool devfs_is_tty(const vnode_t* node);
