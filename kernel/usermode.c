// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <types.h>
#include <usermode.h>
#include <proc.h>
#include <gdt.h>
#include <memory.h>
#include <memcpy.h>
#include <memset.h>
#include <console.h>
#include <keyboard.h>
#include <klog.h>
#include <panic.h>
#include <idt.h>
#include <io.h>
#include <elf.h>
#include <vfs.h>
#include <klibc/string.h>

#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL
#define PAGE_MASK     (~(USER_PAGE_SIZE - 1))

// ---------------------------------------------------------------------------
// User address spaces
// ---------------------------------------------------------------------------

// Only use the NX bit if the CPU has been told about it: with EFER.NXE clear,
// bit 63 of a PTE is reserved and setting it makes every access fault.
static bool nx_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        uint32_t lo, hi;
        __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080));
        cached = (lo >> 11) & 1;
    }
    return cached;
}

bool user_map(process_t* p, u64 virt, u32 pf_flags) {
    virt &= PAGE_MASK;
    u64 pte = vmm_get_pte(p->space, virt);

    if (pte & PTE_PRESENT) {
        // Two segments sharing a page get the union of their rights.
        u64 flags = PTE_PRESENT | PTE_USER;
        if ((pte & PTE_WRITE) || (pf_flags & PF_W)) flags |= PTE_WRITE;
        if ((pte & PTE_NX) && !(pf_flags & PF_X)) flags |= PTE_NX;
        vmm_map_in(p->space, virt, pte & PTE_ADDR_MASK, flags);
        return true;
    }

    if (p->user_pages >= USER_MAX_PAGES) {
        klog(LOG_ERROR, "usermode: %s needs more than %u pages\n", p->name, USER_MAX_PAGES);
        return false;
    }
    void* frame = pmm_alloc();
    if (!frame) {
        klog(LOG_ERROR, "usermode: out of physical memory\n");
        return false;
    }
    // Zeroed through the kernel's direct map: the page need not be in the
    // address space that is currently loaded.
    memset((void*)((u64)frame + vmm_hhdm()), 0, (uint32_t)USER_PAGE_SIZE);

    u64 flags = PTE_PRESENT | PTE_USER;
    if (pf_flags & PF_W) flags |= PTE_WRITE;
    if (nx_enabled() && !(pf_flags & PF_X)) flags |= PTE_NX;
    vmm_map_in(p->space, virt, (u64)frame, flags);
    p->user_pages++;
    return true;
}

bool user_copy_to(process_t* p, u64 virt, const void* src, u64 len) {
    const u8* s = (const u8*)src;
    while (len) {
        u64 pte = vmm_get_pte(p->space, virt);
        if (!(pte & PTE_PRESENT)) return false;
        u64 off = virt & (USER_PAGE_SIZE - 1);
        u64 chunk = USER_PAGE_SIZE - off;
        if (chunk > len) chunk = len;
        memcpy((void*)((pte & PTE_ADDR_MASK) + vmm_hhdm() + off), s, chunk);
        virt += chunk;
        s += chunk;
        len -= chunk;
    }
    return true;
}

bool user_range_ok(u64 addr, u64 len, bool write) {
    if (len == 0) return true;
    if (addr + len < addr) return false;                     // wraparound
    if (addr + len > USER_STACK_TOP) return false;           // kernel half etc.

    u64 space = vmm_current_space();
    u64 last = (addr + len - 1) & PAGE_MASK;
    for (u64 page = addr & PAGE_MASK; ; page += USER_PAGE_SIZE) {
        u64 pte = vmm_get_pte(space, page);
        if (!(pte & PTE_PRESENT) || !(pte & PTE_USER)) return false;
        if (write && !(pte & PTE_WRITE)) return false;
        if (page == last) break;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Starting processes
// ---------------------------------------------------------------------------

// Map the stack and lay argv out at the top of it, System V style:
//
//   USER_STACK_TOP -> | argv strings ...           |
//                     | argv[argc] = NULL          |
//                     | argv[argc-1] ... argv[0]   | <- argv, initial rsp
//
// main(argc, argv) gets them in rdi/rsi straight from the forged frame.
static bool setup_stack(process_t* p, u32 argc, const char* const* argv,
                        u64* sp_out, u64* argv_out) {
    for (u64 page = USER_STACK_BOTTOM; page < USER_STACK_TOP; page += USER_PAGE_SIZE) {
        if (!user_map(p, page, PF_R | PF_W)) return false;
    }

    u64 sp = USER_STACK_TOP;
    u64 ptrs[USER_MAX_ARGS + 1];
    for (int i = (int)argc - 1; i >= 0; i--) {
        u64 len = strlen(argv[i]) + 1;
        sp -= len;
        if (!user_copy_to(p, sp, argv[i], len)) return false;
        ptrs[i] = sp;
    }
    ptrs[argc] = 0;

    sp -= (argc + 1) * sizeof(u64);
    sp &= ~0xFULL;                   // the ABI wants rsp 16-aligned at entry
    if (!user_copy_to(p, sp, ptrs, (argc + 1) * sizeof(u64))) return false;

    *sp_out = sp;
    *argv_out = sp;
    return true;
}

i32 usermode_spawn(const char* path, u32 argc, const char* const* argv, i32 ppid) {
    const char* default_argv[1] = { path };
    if (argc == 0 || !argv) {
        argc = 1;
        argv = default_argv;
    }
    if (argc > USER_MAX_ARGS) return -1;

    u8* image = kmalloc(USER_MAX_IMAGE);
    if (!image) {
        klog(LOG_ERROR, "usermode: out of memory reading '%s'\n", path);
        return -1;
    }
    i32 size = vfs_read_file(path, image, USER_MAX_IMAGE);
    if (size < 0) {
        kfree(image);
        return -1;
    }
    if ((u64)size == USER_MAX_IMAGE) {
        // vfs_read_file stops at the cap, so a full buffer means truncation.
        kfree(image);
        klog(LOG_ERROR, "usermode: '%s' is larger than %u bytes\n", path, USER_MAX_IMAGE);
        return -1;
    }

    // Name the process after the last path component.
    const char* name = path;
    for (const char* c = path; *c; c++) if (*c == '/') name = c + 1;

    process_t* p = proc_alloc(name, ppid);
    if (!p) {
        kfree(image);
        return -1;
    }

    u64 entry, image_end, sp, uargv;
    bool ok = elf_load(p, image, (u64)size, &entry, &image_end);
    kfree(image);                    // segments are copied out; the file is done
    if (!ok || !setup_stack(p, argc, argv, &sp, &uargv)) {
        proc_free(p);
        return -1;
    }

    p->brk_base = p->brk = image_end;
    klog(LOG_DEBUG, "usermode: pid %u '%s' entry 0x%lx, %u pages\n",
         (u32)p->pid, p->name, entry, p->user_pages);
    proc_start(p, entry, sp, argc, uargv);
    return p->pid;
}

i32 usermode_spawn_blob(const char* name, const char* start, const char* end, i32 ppid) {
    u64 len = (u64)(end - start);
    if (len == 0 || len > USER_PAGE_SIZE) {
        klog(LOG_ERROR, "usermode: '%s' is %lu bytes, does not fit in one page\n", name, len);
        return -1;
    }

    process_t* p = proc_alloc(name, ppid);
    if (!p) return -1;

    u64 sp, uargv;
    const char* argv[1] = { name };
    // Read-only and executable: no rewriting its own text.
    if (!user_map(p, USER_CODE_BASE, PF_R | PF_X) ||
        !user_copy_to(p, USER_CODE_BASE, start, len) ||
        !setup_stack(p, 1, argv, &sp, &uargv)) {
        proc_free(p);
        return -1;
    }
    p->brk_base = p->brk = USER_CODE_BASE + USER_PAGE_SIZE;
    proc_start(p, USER_CODE_BASE, sp, 1, uargv);
    return p->pid;
}

void usermode_run_and_wait(i32 pid) {
    if (pid < 0) return;
    // The kernel task cannot block, so it idles until the process is reaped.
    // Interrupts are on here, so the scheduler runs the process meanwhile.
    while (proc_by_pid(pid)) {
        sched_reap_orphans();
        __asm__ volatile("hlt");
    }
}

static void run_blob(const char* name, const char* start, const char* end) {
    u64 flags = irq_save();
    i32 pid = usermode_spawn_blob(name, start, end, 0);
    irq_restore(flags);
    usermode_run_and_wait(pid);
}

void usermode_demo(void) {
    run_blob("hello", user_program_start, user_program_end);
}

void usermode_stack_demo(void) {
    run_blob("stack", user_stack_program_start, user_stack_program_end);
}

void usermode_fault_demo(void) {
    run_blob("faulter", user_fault_program_start, user_fault_program_end);
}

// ---------------------------------------------------------------------------
// Syscalls
// ---------------------------------------------------------------------------

// Ring 3 may hand us any pointer at all. Faulting on a bad one would be a
// kernel-mode page fault, i.e. a panic, so every buffer is proved to be
// mapped user-accessible -- with the right permissions -- before it is
// touched. Syscalls run in the caller's address space, so once checked, a
// user pointer can be used directly.

// Copy a NUL-terminated string out of user memory, one byte at a time so
// every page it crosses gets checked. Fails if it does not end within `max`.
static bool copy_string_from_user(char* dst, u64 src, u64 max) {
    for (u64 i = 0; i < max; i++) {
        if (!user_range_ok(src + i, 1, false)) return false;
        dst[i] = *(const char*)(src + i);
        if (dst[i] == '\0') return true;
    }
    return false;
}

static i64 sys_write(u64 fd, u64 buf, u64 len) {
    if (fd != 1 && fd != 2) return -1;                       // stdout/stderr
    if (!user_range_ok(buf, len, false)) {
        klog(LOG_WARN, "usermode: rejected write of %lu bytes from 0x%lx\n", len, buf);
        return -1;
    }
    const char* p = (const char*)buf;
    for (u64 i = 0; i < len; i++) {
        terminal_putchar((unsigned char)p[i]);
    }
    terminal_flush();
    return (i64)len;
}

// Reads one line from the keyboard, without the trailing newline. The
// keyboard IRQ collects keys into input_buffer and raises command_ready on
// Enter, waking whoever is blocked here.
static i64 sys_read(u64 fd, u64 buf, u64 len) {
    if (fd != 0) return -1;                                  // stdin only
    if (!user_range_ok(buf, len, true)) {
        klog(LOG_WARN, "usermode: rejected read of %lu bytes into 0x%lx\n", len, buf);
        return -1;
    }

    if (!command_ready) {
        if (index == 0) prompt_x = cursor_x;   // backspace may not eat the prompt
        terminal_flush();
        while (!command_ready) {
            sched_block(WAIT_INPUT);
        }
    }
    command_ready = false;
    terminal_putchar('\n');          // the keyboard does not echo Enter
    terminal_flush();

    u64 n = strlen(input_buffer);
    if (n > len) n = len;
    memcpy((void*)buf, input_buffer, n);
    return (i64)n;
}

static i64 sys_readfile(u64 path, u64 buf, u64 len) {
    char kpath[256];
    if (!copy_string_from_user(kpath, path, sizeof(kpath))) return -1;
    if (len > 0xFFFFFFFFULL) len = 0xFFFFFFFFULL;            // the VFS takes a u32
    if (!user_range_ok(buf, len, true)) {
        klog(LOG_WARN, "usermode: rejected readfile of %lu bytes into 0x%lx\n", len, buf);
        return -1;
    }
    return vfs_read_file(kpath, (void*)buf, (uint32_t)len);
}

static i64 sys_spawn(u64 path, u64 argv) {
    char kpath[256];
    if (!copy_string_from_user(kpath, path, sizeof(kpath))) return -1;

    // Pull argv into the kernel: the pointers, then the strings they name.
    char strings[USER_MAX_ARGBYTES];
    const char* kargv[USER_MAX_ARGS];
    u32 argc = 0;
    u64 used = 0;
    if (argv) {
        for (;;) {
            if (!user_range_ok(argv + argc * 8, 8, false)) return -1;
            u64 s = *(const u64*)(argv + argc * 8);
            if (!s) break;
            if (argc == USER_MAX_ARGS) return -1;
            if (!copy_string_from_user(strings + used, s, sizeof(strings) - used)) return -1;
            kargv[argc++] = strings + used;
            used += strlen(strings + used) + 1;
        }
    }
    return usermode_spawn(kpath, argc, kargv, proc_current()->pid);
}

static i64 sys_wait(u64 pid, u64 status_ptr) {
    if (status_ptr && !user_range_ok(status_ptr, sizeof(i64), true)) return -1;
    i64 status = 0;
    i32 r = proc_wait((i32)pid, &status);
    if (r >= 0 && status_ptr) *(i64*)status_ptr = status;
    return r;
}

static i64 sys_sbrk(i64 increment) {
    process_t* p = proc_current();
    u64 old = p->brk;
    if (increment == 0) return (i64)old;
    if (increment < 0) return -1;                            // no shrinking yet
    u64 new_brk = old + (u64)increment;
    if (new_brk < old || new_brk > USER_LOAD_LIMIT) return -1;

    u64 first = (old + USER_PAGE_SIZE - 1) & PAGE_MASK;
    for (u64 page = first; page < new_brk; page += USER_PAGE_SIZE) {
        if (!user_map(p, page, PF_R | PF_W)) return -1;
    }
    p->brk = new_brk;
    return (i64)old;
}

static i64 sys_sleep(u64 ms) {
    if (ms == 0) return 0;
    u64 n = (ms * 250 + 999) / 1000;                         // PIT runs at 250 Hz
    if (n > 0xFFFFFFFFULL) n = 0xFFFFFFFFULL;
    sched_sleep((u32)n);
    return 0;
}

static i64 sys_readdir(u64 path, u64 index, u64 out) {
    char kpath[256] = ".";
    if (path && !copy_string_from_user(kpath, path, sizeof(kpath))) return -1;
    if (kpath[0] == '\0') { kpath[0] = '.'; kpath[1] = '\0'; }
    if (!user_range_ok(out, sizeof(user_dirent_t), true)) return -1;

    user_dirent_t d;
    memset(&d, 0, sizeof(d));
    bool is_dir;
    i32 r = vfs_readdir(kpath, (u32)index, d.name, &d.size, &is_dir);
    if (r == 1) {
        d.is_dir = is_dir;
        memcpy((void*)out, &d, sizeof(d));
    }
    return r;
}

static i64 sys_chdir(u64 path) {
    char kpath[256];
    if (!copy_string_from_user(kpath, path, sizeof(kpath))) return -1;
    return vfs_chdir(kpath) ? 0 : -1;
}

static i64 sys_getcwd(u64 buf, u64 len) {
    const char* cwd = vfs_pwd();
    u64 n = strlen(cwd) + 1;
    if (n > len || !user_range_ok(buf, n, true)) return -1;
    memcpy((void*)buf, cwd, n);
    return (i64)(n - 1);
}

static i64 sys_procinfo(u64 index, u64 out) {
    if (!user_range_ok(out, sizeof(proc_info_t), true)) return -1;
    proc_info_t info;
    if (!proc_info((u32)index, &info)) return 0;
    memcpy((void*)out, &info, sizeof(info));
    return 1;
}

void syscall_dispatch(registers_t* regs) {
    u64 a = regs->rdi, b = regs->rsi, c = regs->rdx;
    i64 r;

    switch (regs->rax) {
        case SYS_EXIT:     proc_exit((i64)a);                    // does not return
        case SYS_WRITE:    r = sys_write(a, b, c); break;
        case SYS_GETCPL:   r = (i64)(regs->cs & 3); break;       // CPL = low bits of CS
        case SYS_READ:     r = sys_read(a, b, c); break;
        case SYS_READFILE: r = sys_readfile(a, b, c); break;
        case SYS_SPAWN:    r = sys_spawn(a, b); break;
        case SYS_WAIT:     r = sys_wait(a, b); break;
        case SYS_GETPID:   r = proc_current()->pid; break;
        case SYS_SBRK:     r = sys_sbrk((i64)a); break;
        case SYS_YIELD:    schedule(); r = 0; break;
        case SYS_SLEEP:    r = sys_sleep(a); break;
        case SYS_READDIR:  r = sys_readdir(a, b, c); break;
        case SYS_CHDIR:    r = sys_chdir(a); break;
        case SYS_GETCWD:   r = sys_getcwd(a, b); break;
        case SYS_PROCINFO: r = sys_procinfo(a, b); break;
        default:
            klog(LOG_WARN, "usermode: pid %u called unknown syscall %lu\n",
                 (u32)proc_current()->pid, regs->rax);
            r = -1;
            break;
    }
    regs->rax = (u64)r;
}

// ---------------------------------------------------------------------------
// Faults
// ---------------------------------------------------------------------------

void usermode_fault(const char* reason, registers_t* regs) {
    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    process_t* p = proc_current();

    terminal_printf(RED, BLACK, "\n%s (pid %u): %s\n", p->name, (u32)p->pid, reason);
    terminal_printf(SILVER, BLACK,
        "  rip=0x%016lx rsp=0x%016lx err=0x%lx cr2=0x%016lx\n",
        regs->rip, regs->rsp, regs->err_code, cr2);

    // Like a Unix shell reports a signal: 128 + the vector that killed it.
    proc_exit(128 + (i64)regs->int_no);                      // does not return
}
