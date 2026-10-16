// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

// The native syscall handlers that personalities reuse directly where their
// semantics already agree with Kunix's. Arguments are raw user values;
// results are >= 0 or -errno. Pointers are validated inside.

#include <types.h>
#include <idt.h>
#include <vfs.h>
#include <syscall.h>

i32 copy_path(char* dst, u64 user_path);         // 0, -EFAULT, -ENAMETOOLONG
void fill_stat(const vnode_t* n, kstat_t* st);

i64 sys_read(u64 fd, u64 buf, u64 len);
i64 sys_write(u64 fd, u64 buf, u64 len);
i64 sys_open(u64 path, u64 flags, u64 mode);
i64 sys_lseek(u64 fd, i64 off, u64 whence);
i64 sys_dup(u64 fd);
i64 sys_dup2(u64 oldfd, u64 newfd);
i64 sys_pipe(u64 fds);
i64 sys_path_op(u64 path, u64 op);               // op: SYS_MKDIR/UNLINK/RMDIR
i64 sys_ftruncate(u64 fd, i64 len);
i64 sys_chdir(u64 path);
i64 sys_getcwd(u64 buf, u64 len);                // length without the NUL
i64 sys_execve(registers_t* regs, u64 path, u64 argv, u64 envp);
i64 sys_waitpid(u64 pid, u64 status, u64 options);
i64 sys_sbrk(i64 increment);
i64 sys_kill(i64 pid, u64 sig);
i64 sys_setpgid(i64 pid, i64 pgid);
i64 sys_getpgid(i64 pid);
i64 sys_rename(u64 oldpath, u64 newpath);
