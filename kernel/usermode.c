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
#include <serial.h>
#include <panic.h>
#include <idt.h>
#include <io.h>
#include <elf.h>
#include <vfs.h>
#include <file.h>
#include <signal.h>
#include <errno.h>
#include <klibc/string.h>
#include <mman.h>

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
        return vmm_map_in(p->space, virt, pte & PTE_ADDR_MASK, flags);
    }

    if (p->user_pages >= USER_MAX_PAGES) {
        kserialoutf("usermode: %s needs more than %u pages\n", p->name, USER_MAX_PAGES);
        return false;
    }
    void* frame = pmm_alloc();
    if (!frame) {
        kserialoutf("usermode: out of physical memory\n");
        return false;
    }
    // Zeroed through the kernel's direct map: the page need not be in the
    // address space that is currently loaded.
    memset((void*)((u64)frame + vmm_hhdm()), 0, (uint32_t)USER_PAGE_SIZE);

    u64 flags = PTE_PRESENT | PTE_USER;
    if (pf_flags & PF_W) flags |= PTE_WRITE;
    if (nx_enabled() && !(pf_flags & PF_X)) flags |= PTE_NX;
    if (!vmm_map_in(p->space, virt, (u64)frame, flags)) {
        pmm_free((u64)frame);
        kserialoutf("usermode: out of memory for page tables\n");
        return false;
    }
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

// The stack region below what is mapped grows on first touch -- from a page
// fault in ring 3, or from a syscall handed a buffer there. True if `addr`
// is now backed.
bool user_grow_stack(u64 addr) {
    if (addr < USER_STACK_LIMIT || addr >= USER_STACK_BOTTOM) return false;
    process_t* p = proc_current();
    if (p->pid == 0) return false;
    return user_map(p, addr & PAGE_MASK, PF_R | PF_W);
}

bool user_range_ok(u64 addr, u64 len, bool write) {
    if (len == 0) return true;
    if (addr + len < addr) return false;                     // wraparound
    if (addr + len > USER_STACK_TOP) return false;           // kernel half etc.

    u64 space = vmm_current_space();
    u64 last = (addr + len - 1) & PAGE_MASK;
    for (u64 page = addr & PAGE_MASK; ; page += USER_PAGE_SIZE) {
        u64 pte = vmm_get_pte(space, page);
        // Not there yet (lazy mmap, stack growth) or copy-on-write: fault
        // it in now, exactly as the program touching it would have.
        if ((!(pte & PTE_PRESENT) || (write && !(pte & PTE_WRITE))) && mm_fault(page, write)) {
            pte = vmm_get_pte(space, page);
        }
        if (!(pte & PTE_PRESENT) || !(pte & PTE_USER)) return false;
        if (write && !(pte & PTE_WRITE)) return false;
        if (page == last) break;
    }
    return true;
}

bool copy_string_from_user(char* dst, u64 src, u64 max) {
    for (u64 i = 0; i < max; i++) {
        if (!user_range_ok(src + i, 1, false)) return false;
        dst[i] = *(const char*)(src + i);
        if (dst[i] == '\0') return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Loading images
// ---------------------------------------------------------------------------

// Map the stack and lay out argv and envp at the top of it, System V style:
//
//   USER_STACK_TOP -> | the strings ...                          |
//                     | envp[0] ... envp[envc-1], NULL           | <- envp
//                     | argv[0] ... argv[argc-1], NULL           | <- argv, rsp
//
// main(argc, argv, envp) gets them in rdi/rsi/rdx from the entry frame.
static bool setup_stack(process_t* p, const user_args_t* a, u64* sp_out,
                        u64* argv_out, u64* envp_out) {
    for (u64 page = USER_STACK_BOTTOM; page < USER_STACK_TOP; page += USER_PAGE_SIZE) {
        if (!user_map(p, page, PF_R | PF_W)) return false;
    }

    u64 sp = USER_STACK_TOP;
    u64 ptrs[USER_MAX_ARGS + 1 + USER_MAX_ARGS + 1];
    u32 n = 0;
    for (u32 i = 0; i < a->argc; i++) {
        u64 len = strlen(a->argv[i]) + 1;
        sp -= len;
        if (!user_copy_to(p, sp, a->argv[i], len)) return false;
        ptrs[n++] = sp;
    }
    ptrs[n++] = 0;
    for (u32 i = 0; i < a->envc; i++) {
        u64 len = strlen(a->envp[i]) + 1;
        sp -= len;
        if (!user_copy_to(p, sp, a->envp[i], len)) return false;
        ptrs[n++] = sp;
    }
    ptrs[n++] = 0;

    sp -= n * sizeof(u64);
    sp &= ~0xFULL;                   // the ABI wants rsp 16-aligned at entry
    if (!user_copy_to(p, sp, ptrs, n * sizeof(u64))) return false;

    *sp_out = sp;
    *argv_out = sp;
    *envp_out = sp + (a->argc + 1) * sizeof(u64);
    return true;
}

// Read an executable and map it, plus a stack carrying args, into p's
// address space (whatever p->space currently is).
static i32 load_into(process_t* p, const char* path, const user_args_t* args,
                     registers_t* frame) {
    vnode_t node;
    i32 r = vfs_lookup(path, &node);
    if (r < 0) return r;
    if (node.type != VN_REG) return -EACCES;
    if (node.size > USER_MAX_IMAGE) return -E2BIG;

    u8* image = kmalloc(node.size ? node.size : 1);
    if (!image) return -ENOMEM;
    i64 size = vfs_read(&node, 0, image, node.size);
    if (size < 0) {
        kfree(image);
        return (i32)size;
    }

    u64 entry, image_end, sp, uargv, uenvp;
    bool ok = elf_load(p, image, (u64)size, &entry, &image_end);
    kfree(image);                    // segments are copied out; the file is done
    if (!ok) return -ENOEXEC;
    if (!setup_stack(p, args, &sp, &uargv, &uenvp)) return -ENOMEM;

    p->brk_base = p->brk = image_end;
    memset(frame, 0, sizeof(*frame));
    frame->rip = entry;
    frame->rsp = sp;
    frame->eflags = 0x202;
    // execve hands this frame straight to iretq, so it must be complete.
    frame->cs = GDT_USER_CS_RPL3;
    frame->ss = GDT_USER_DS_RPL3;
    frame->rdi = args->argc;
    frame->rsi = uargv;
    frame->rdx = uenvp;
    return 0;
}

static void set_name(process_t* p, const char* path) {
    const char* name = path;
    for (const char* c = path; *c; c++) if (*c == '/') name = c + 1;
    u32 i = 0;
    for (; name[i] && i < PROC_NAME_LEN - 1; i++) p->name[i] = name[i];
    p->name[i] = '\0';
}

i32 usermode_spawn(const char* path, const user_args_t* args, process_t* parent) {
    user_args_t defaults = { .argc = 1, .argv = { path }, .envc = 0 };
    if (!args || args->argc == 0) args = &defaults;

    process_t* p = proc_alloc(path, parent);
    if (!p) return -ENOMEM;
    set_name(p, path);

    registers_t frame;
    i32 r = load_into(p, path, args, &frame);
    if (r < 0) {
        proc_free(p);
        return r;
    }
    kserialoutf("usermode: pid %u '%s' entry 0x%lx, %u pages\n",
         (u32)p->pid, p->name, frame.rip, p->user_pages);
    proc_start_frame(p, &frame);
    return p->pid;
}

i32 usermode_exec(const char* path, const user_args_t* args, registers_t* regs) {
    process_t* p = proc_current();

    // Build the new image in a fresh address space, so that if anything
    // fails the caller is still intact and just gets an error back.
    u64 old_space = p->space;
    u32 old_pages = p->user_pages;
    u64 old_brk_base = p->brk_base, old_brk = p->brk;
    u64 new_space = vmm_create_space();
    if (!new_space) return -ENOMEM;
    p->space = new_space;
    p->user_pages = 0;

    registers_t frame;
    i32 r = load_into(p, path, args, &frame);
    if (r < 0) {
        vmm_destroy_space(new_space);
        p->space = old_space;
        p->user_pages = old_pages;
        p->brk_base = old_brk_base;
        p->brk = old_brk;
        return r;
    }

    // Point of no return: the old image goes away.
    vmm_switch(new_space);
    vmm_destroy_space(old_space);
    p->nvmas = 0;
    set_name(p, path);
    fd_close_on_exec(p);
    for (int s = 0; s < NSIG; s++) {
        if (p->sig_handler[s] != SIG_IGN) p->sig_handler[s] = SIG_DFL;
    }
    p->sig_restorer = 0;
    proc_fpu_reset();
    *regs = frame;
    return 0;
}

i32 usermode_spawn_blob(const char* name, const char* start, const char* end) {
    u64 len = (u64)(end - start);
    if (len == 0 || len > USER_PAGE_SIZE) return -E2BIG;

    process_t* p = proc_alloc(name, NULL);
    if (!p) return -ENOMEM;

    u64 sp, uargv, uenvp;
    user_args_t args = { .argc = 1, .argv = { name }, .envc = 0 };
    // Read-only and executable: no rewriting its own text.
    if (!user_map(p, USER_CODE_BASE, PF_R | PF_X) ||
        !user_copy_to(p, USER_CODE_BASE, start, len) ||
        !setup_stack(p, &args, &sp, &uargv, &uenvp)) {
        proc_free(p);
        return -ENOMEM;
    }
    p->brk_base = p->brk = USER_CODE_BASE + USER_PAGE_SIZE;
    proc_start(p, USER_CODE_BASE, sp, 1, uargv, uenvp);
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
    i32 pid = usermode_spawn_blob(name, start, end);
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
// Faults
// ---------------------------------------------------------------------------

void usermode_fault(const char* reason, registers_t* regs) {
    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    process_t* p = proc_current();

    // A page fault that is really demand paging -- the stack growing, an
    // mmap region's first touch, a write to a copy-on-write page -- is
    // resolved and the instruction retried. (Error code bit 1: a write.)
    if (regs->int_no == 14 && mm_fault(cr2, (regs->err_code & 2) != 0)) return;
    i32 sig = signal_for_fault(regs->int_no);

    // A fault cannot be ignored or blocked: retrying the instruction would
    // just fault again forever. Like Linux, force the default in that case.
    if (p->sig_handler[sig] == SIG_IGN || (p->sig_mask & (1u << sig))) {
        p->sig_handler[sig] = SIG_DFL;
        p->sig_mask &= ~(1u << sig);
    }

    if (p->sig_handler[sig] == SIG_DFL) {
        terminal_printf(RED, BLACK, "\n%s (pid %u): %s\n", p->name, (u32)p->pid, reason);
        terminal_printf(SILVER, BLACK,
            "  rip=0x%016lx rsp=0x%016lx err=0x%lx cr2=0x%016lx\n",
            regs->rip, regs->rsp, regs->err_code, cr2);
    }
    signal_send(p, sig);
    signal_deliver(regs);            // kills, or redirects into the handler
}
