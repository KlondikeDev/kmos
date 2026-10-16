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
#include <msr.h>

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

// Auxiliary vector entries (the System V / Linux numbering).
#define AT_NULL     0
#define AT_PHDR     3
#define AT_PHENT    4
#define AT_PHNUM    5
#define AT_PAGESZ   6
#define AT_BASE     7
#define AT_FLAGS    8
#define AT_ENTRY    9
#define AT_UID      11
#define AT_EUID     12
#define AT_GID      13
#define AT_EGID     14
#define AT_PLATFORM 15
#define AT_HWCAP    16
#define AT_CLKTCK   17
#define AT_SECURE   23
#define AT_RANDOM   25
#define AT_EXECFN   31
#define AUXV_COUNT  18           // pairs, AT_NULL included

// Not cryptographic: the timestamp counter, stirred (splitmix64). It seeds
// libc's stack-protector canary and the like; good enough until there is a
// real entropy source.
u64 weak_random(void) {
    static u64 state;
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    state += ((u64)hi << 32 | lo) + 0x9E3779B97F4A7C15ULL;
    u64 z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static u64 cpu_hwcap(void) {
    u32 a = 1, b, c, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    return d;
}

// Copy a string just below *sp; returns its user address.
static u64 push_string(process_t* p, u64* sp, const char* str) {
    u64 len = strlen(str) + 1;
    *sp -= len;
    return user_copy_to(p, *sp, str, len) ? *sp : 0;
}

// Map the stack and lay out the initial process state at its top, the
// System V way -- which is what Linux programs read from rsp, while Kunix's
// own crt0 just takes argc/argv/envp from rdi/rsi/rdx:
//
//   USER_STACK_TOP -> | strings: argv, envp, execfn, platform; 16 random bytes |
//                     | (padding to 16 bytes)                                   |
//                     | auxv: (type, value) pairs, ending with AT_NULL          |
//                     | NULL, envp[envc-1] ... envp[0]                          |
//                     | NULL, argv[argc-1] ... argv[0]                          |
//             rsp ->  | argc                                                    |
static bool setup_stack(process_t* p, u32 argc, const char* const* argv, u32 envc,
                        const char* const* envp, const char* execfn,
                        const elf_info_t* info, registers_t* frame) {
    u64 strings = strlen(execfn) + 1 + 7 + 16 + 16;
    for (u32 i = 0; i < argc; i++) strings += strlen(argv[i]) + 1;
    for (u32 i = 0; i < envc; i++) strings += strlen(envp[i]) + 1;
    u64 words = 1 + (argc + 1) + (envc + 1) + 2 * AUXV_COUNT;
    u64 need = strings + words * 8 + 32;

    u64 bottom = USER_STACK_BOTTOM;
    while (USER_STACK_TOP - bottom < need + USER_PAGE_SIZE) bottom -= USER_PAGE_SIZE;
    for (u64 page = bottom; page < USER_STACK_TOP; page += USER_PAGE_SIZE) {
        if (!user_map(p, page, PF_R | PF_W)) return false;
    }

    u64* w = kmalloc(words * 8);
    if (!w) return false;
    bool ok = false;
    u64 sp = USER_STACK_TOP;
    u32 n = 0;
    // Strings are pushed last-first, so they end up in order in memory:
    // argv[0]'s NUL-separated run is what /proc/<pid>/cmdline shows.
    w[n++] = argc;
    u64 uenvp_index = 1 + argc + 1;
    for (u32 i = envc; i-- > 0; ) if (!(w[uenvp_index + i] = push_string(p, &sp, envp[i]))) goto out;
    u64 args_end = sp;
    for (u32 i = argc; i-- > 0; ) if (!(w[1 + i] = push_string(p, &sp, argv[i]))) goto out;
    p->arg_start = sp;
    p->arg_end = args_end;
    n = 1 + argc;
    w[n++] = 0;
    n += envc;
    w[n++] = 0;
    u64 uexecfn = push_string(p, &sp, execfn);
    u64 uplatform = push_string(p, &sp, "x86_64");
    u64 rnd[2] = { weak_random(), weak_random() };
    sp = (sp - 16) & ~0xFULL;
    if (!uexecfn || !uplatform || !user_copy_to(p, sp, rnd, 16)) goto out;
    u64 urandom = sp;

    u64 aux[] = {
        AT_PHDR, info ? info->phdr : 0,   AT_PHENT, sizeof(elf64_phdr_t),
        AT_PHNUM, info ? info->phnum : 0, AT_PAGESZ, USER_PAGE_SIZE,
        AT_BASE, 0,                       AT_FLAGS, 0,
        AT_ENTRY, info ? info->entry : 0, AT_UID, p->uid,
        AT_EUID, p->exec_euid,            AT_GID, p->gid,
        AT_EGID, p->exec_egid,            AT_PLATFORM, uplatform,
        AT_HWCAP, cpu_hwcap(),            AT_CLKTCK, 100,
        // A set-id program: libc must not trust the environment.
        AT_SECURE, p->exec_euid != p->uid || p->exec_egid != p->gid,
                                          AT_RANDOM, urandom,
        AT_EXECFN, uexecfn,
        AT_NULL, 0,
    };
    _Static_assert(sizeof(aux) == AUXV_COUNT * 16, "AUXV_COUNT must match the vector");
    for (u32 i = 0; i < AUXV_COUNT * 2; i++) w[n++] = aux[i];

    // rsp must be 16-aligned at entry, pointing at argc.
    sp -= n * 8;
    sp &= ~0xFULL;
    if (!user_copy_to(p, sp, w, n * 8)) goto out;

    frame->rsp = sp;
    frame->rdi = argc;
    frame->rsi = sp + 8;
    frame->rdx = sp + uenvp_index * 8;
    ok = true;
out:
    kfree(w);
    return ok;
}

static i32 load_depth(process_t* p, const char* path, u32 argc, const char* const* argv,
                      u32 envc, const char* const* envp, registers_t* frame, u32 depth);

#define SHEBANG_MAX   128      // the "#!interpreter arg" line, newline included
#define SHEBANG_DEPTH 4        // scripts whose interpreter is a script...

// A "#!" script: run its interpreter instead, as
//   interpreter [optional-arg] script argv[1] ... argv[argc-1]
// The optional argument is everything after the interpreter's name, as one
// word -- Linux's (and most systems') rule.
static i32 load_script(process_t* p, const char* path, char* head, u32 argc,
                       const char* const* argv, u32 envc, const char* const* envp,
                       registers_t* frame, u32 depth) {
    if (depth >= SHEBANG_DEPTH) return -ENOEXEC;
    char* nl = head;
    while (*nl && *nl != '\n') nl++;
    if (!*nl) return -ENOEXEC;                         // no newline: line too long
    *nl = '\0';
    char* s = head + 2;
    while (*s == ' ' || *s == '\t') s++;
    char* interp = s;
    while (*s && *s != ' ' && *s != '\t') s++;
    if (*s) *s++ = '\0';
    while (*s == ' ' || *s == '\t') s++;
    char* opt = s;
    char* end = opt + strlen(opt);
    while (end > opt && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) *--end = '\0';
    if (!*interp) return -ENOEXEC;

    u32 n = 1 + (*opt ? 1 : 0) + 1 + (argc > 1 ? argc - 1 : 0);
    if (n > USER_MAX_ARGS) return -E2BIG;
    const char** v = kmalloc(n * sizeof(char*));
    if (!v) return -ENOMEM;
    u32 i = 0;
    v[i++] = interp;
    if (*opt) v[i++] = opt;
    v[i++] = path;
    for (u32 k = 1; k < argc; k++) v[i++] = argv[k];
    i32 r = load_depth(p, interp, n, v, envc, envp, frame, depth + 1);
    kfree(v);
    return r;
}

// Read an executable and map it, plus a stack carrying args, into p's
// address space (whatever p->space currently is).
static i32 load_into(process_t* p, const char* path, u32 argc, const char* const* argv,
                     u32 envc, const char* const* envp, registers_t* frame) {
    return load_depth(p, path, argc, argv, envc, envp, frame, 0);
}

static i32 load_depth(process_t* p, const char* path, u32 argc, const char* const* argv,
                      u32 envc, const char* const* envp, registers_t* frame, u32 depth) {
    vnode_t node;
    i32 r = vfs_lookup(path, &node);
    if (r < 0) return r;
    if (node.type != VN_REG) return -EACCES;
    // Even root needs an execute bit -- where the filesystem keeps them.
    if (!vfs_may(&node, VFS_X)) return -EACCES;
    if (node.size > USER_MAX_IMAGE) return -E2BIG;

    char head[SHEBANG_MAX];
    i64 got = vfs_read(&node, 0, head, sizeof(head) - 1);
    if (got >= 2 && head[0] == '#' && head[1] == '!') {
        head[got] = '\0';
        return load_script(p, path, head, argc, argv, envc, envp, frame, depth);
    }

    elf_info_t info;
    r = elf_load(p, &node, &info);
    if (r < 0) return r;

    memset(frame, 0, sizeof(*frame));
    // Set-user-ID and set-group-ID programs run with their owner's ids
    // (set-group-ID only with a group execute bit; without, it is a locking
    // marker). Applied by the caller once nothing more can fail.
    u32 mode, fuid, fgid;
    vfs_attrs(&node, &mode, &fuid, &fgid);
    p->exec_euid = (mode & 04000) ? fuid : p->euid;
    p->exec_egid = ((mode & 02000) && (mode & 0010)) ? fgid : p->egid;
    if (!setup_stack(p, argc, argv, envc, envp, path, &info, frame)) return -ENOMEM;
    p->brk_base = p->brk = info.image_end;
    frame->rip = info.entry;
    frame->eflags = 0x202;
    // execve hands this frame straight to iretq, so it must be complete.
    frame->cs = GDT_USER_CS_RPL3;
    frame->ss = GDT_USER_DS_RPL3;
    return 0;
}

static void apply_exec_creds(process_t* p) {
    p->euid = p->suid = p->exec_euid;
    p->egid = p->sgid = p->exec_egid;
}

static void set_exe(process_t* p, const char* path) {
    if (vfs_normalize(path, p->exe) < 0) p->exe[0] = '\0';
}

static void set_name(process_t* p, const char* path) {
    const char* name = path;
    for (const char* c = path; *c; c++) if (*c == '/') name = c + 1;
    u32 i = 0;
    for (; name[i] && i < PROC_NAME_LEN - 1; i++) p->name[i] = name[i];
    p->name[i] = '\0';
}

i32 usermode_spawn(const char* path, const user_args_t* args, process_t* parent) {
    process_t* p = proc_alloc(path, parent);
    if (!p) return -ENOMEM;
    set_name(p, path);
    set_exe(p, path);

    registers_t frame;
    i32 r = args && args->argc
        ? load_into(p, path, args->argc, args->argv, args->envc, args->envp, &frame)
        : load_into(p, path, 1, &path, 0, NULL, &frame);
    if (r < 0) {
        proc_free(p);
        return r;
    }
    apply_exec_creds(p);
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
    u64 old_arg_start = p->arg_start, old_arg_end = p->arg_end;
    u64 new_space = vmm_create_space();
    if (!new_space) return -ENOMEM;
    p->space = new_space;
    p->user_pages = 0;

    registers_t frame;
    i32 r = load_into(p, path, args->argc, args->argv, args->envc, args->envp, &frame);
    if (r < 0) {
        vmm_destroy_space(new_space);
        p->space = old_space;
        p->user_pages = old_pages;
        p->brk_base = old_brk_base;
        p->arg_start = old_arg_start;
        p->arg_end = old_arg_end;
        p->brk = old_brk;
        return r;
    }

    // Point of no return: the old image goes away.
    vmm_switch(new_space);
    vmm_destroy_space(old_space);
    apply_exec_creds(p);
    p->nvmas = 0;
    set_name(p, path);
    set_exe(p, path);
    fd_close_on_exec(p);
    for (int s = 0; s < NSIG; s++) {
        if (p->sig_handler[s] != SIG_IGN) p->sig_handler[s] = SIG_DFL;
    }
    p->sig_restorer = 0;
    p->sig_linux = 0;
    for (int s = 0; s < NSIG; s++) {
        p->sig_flags[s] = p->sig_actmask[s] = 0;
        p->sig_restorers[s] = 0;
    }
    p->fs_base = 0;
    wrmsr(MSR_FS_BASE, 0);
    proc_fpu_reset();
    *regs = frame;
    return 0;
}

i32 usermode_spawn_blob(const char* name, const char* start, const char* end) {
    u64 len = (u64)(end - start);
    if (len == 0 || len > USER_PAGE_SIZE) return -E2BIG;

    process_t* p = proc_alloc(name, NULL);
    if (!p) return -ENOMEM;

    registers_t frame;
    memset(&frame, 0, sizeof(frame));
    // Read-only and executable: no rewriting its own text.
    if (!user_map(p, USER_CODE_BASE, PF_R | PF_X) ||
        !user_copy_to(p, USER_CODE_BASE, start, len) ||
        !setup_stack(p, 1, &name, 0, NULL, name, NULL, &frame)) {
        proc_free(p);
        return -ENOMEM;
    }
    p->brk_base = p->brk = USER_CODE_BASE + USER_PAGE_SIZE;
    frame.rip = USER_CODE_BASE;
    frame.eflags = 0x202;
    proc_start_frame(p, &frame);
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

    kserialoutf("fault: %s (pid %u): %s rip=0x%lx rsp=0x%lx err=0x%lx cr2=0x%lx\n",
                p->name, (u32)p->pid, reason, regs->rip, regs->rsp, regs->err_code, cr2);
    if (p->sig_handler[sig] == SIG_DFL) {
        terminal_printf(RED, BLACK, "\n%s (pid %u): %s\n", p->name, (u32)p->pid, reason);
        terminal_printf(SILVER, BLACK,
            "  rip=0x%016lx rsp=0x%016lx err=0x%lx cr2=0x%016lx\n",
            regs->rip, regs->rsp, regs->err_code, cr2);
    }
    signal_send(p, sig);
    signal_deliver(regs);            // kills, or redirects into the handler
}
