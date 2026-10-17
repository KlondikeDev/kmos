// This file has been generated in whole or in part by a Large Language Model
// uACPI host services. Only PID0 enters the interpreter. SCI handlers are
// polled with the PIC line masked; deferred AML is drained in process context.
#include <uacpi/kernel_api.h>
#include <acpi.h>
#include <memory.h>
#include <io.h>
#include <ktime.h>
#include <klog.h>
#include <battery.h>
#include <limine.h>

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr* out) {
    *out = acpi_rsdp_physical();
    return *out ? UACPI_STATUS_OK : UACPI_STATUS_NOT_FOUND;
}
// Reusable slots avoid exhausting the virtual window on repeated opregion IO.
#define MAP_SLOTS 256
#define MAP_STRIDE 0x200000ULL
static bool maps[MAP_SLOTS];
void* uacpi_kernel_map(uacpi_phys_addr address, uacpi_size size) {
    u64 offset = address & 4095, base = address & ~4095ULL;
    if (!size || size > MAP_STRIDE-offset || address >= (1ULL<<52) ||
        size > (1ULL<<52)-address) return UACPI_MAP_FAILED;
    u64 flags = irq_save();
    u32 slot;
    for (slot=0; slot<MAP_SLOTS && maps[slot]; slot++);
    if (slot == MAP_SLOTS) { irq_restore(flags); return UACPI_MAP_FAILED; }
    maps[slot] = true;
    u64 virt = 0xffffc20000000000ULL + slot*MAP_STRIDE;
    u64 length = (offset+size+4095)&~4095ULL;
    for (u64 i=0; i<length; i+=4096) {
        u64 attributes = 0x1b;
        struct limine_memmap_response* map = memmap_request.response;
        for (u64 j = 0; map && j < map->entry_count; j++) {
            struct limine_memmap_entry* e = map->entries[j];
            if (base+i < e->base || base+i-e->base >= e->length) continue;
            if (e->type == LIMINE_MEMMAP_USABLE || e->type == LIMINE_MEMMAP_ACPI_RECLAIMABLE ||
                e->type == LIMINE_MEMMAP_ACPI_NVS || e->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
                e->type == LIMINE_MEMMAP_KERNEL_AND_MODULES) attributes = 3;
            break;
        }
        if (!vmm_map(virt+i,base+i,attributes)) {
            while(i) { i-=4096; vmm_unmap(virt+i); }
            maps[slot]=false; irq_restore(flags); return UACPI_MAP_FAILED;
        }
    }
    irq_restore(flags);
    return (void*)(virt+offset);
}
void uacpi_kernel_unmap(void* p,uacpi_size size) {
    u64 address=(u64)p, base=address&~4095ULL;
    u64 length=((address&4095)+size+4095)&~4095ULL;
    u64 flags=irq_save();
    for(u64 i=0;i<length;i+=4096) vmm_unmap(base+i);
    maps[(address-0xffffc20000000000ULL)/MAP_STRIDE]=false;
    irq_restore(flags);
}
void uacpi_kernel_log(uacpi_log_level level,const char* text) {
    klog(level==UACPI_LOG_ERROR ? LOG_WARN : LOG_DEBUG,"uacpi: %s",text);
}
void* uacpi_kernel_alloc(uacpi_size size) {
    u64 f=irq_save(); void* p=kmalloc(size); irq_restore(f); return p;
}
void uacpi_kernel_free(void* p) {
    u64 f=irq_save(); kfree(p); irq_restore(f);
}
uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) { return time_counter_ns(); }
void uacpi_kernel_stall(uacpi_u8 us) {
    u64 end=time_counter_ns()+(u64)us*1000;
    while(time_counter_ns()<end) __asm__ volatile("pause");
}
void uacpi_kernel_sleep(uacpi_u64 ms) {
    // Firmware cannot monopolize the kernel with an unbounded Sleep operand.
    if(ms>2000) ms=2000;
    u64 end=time_counter_ns()+ms*1000000;
    while(time_counter_ns()<end) __asm__ volatile("pause");
}
uacpi_handle uacpi_kernel_create_semaphore(uacpi_u32 units) {
    u32* p=uacpi_kernel_alloc(sizeof(*p)); if(p)*p=units; return p;
}
void uacpi_kernel_free_semaphore(uacpi_handle h) { uacpi_kernel_free(h); }
uacpi_status uacpi_kernel_wait_for_semaphore(uacpi_handle h,uacpi_u16 ms) {
    u32* p=h;
    u64 end=time_counter_ns()+(u64)(ms>2000?2000:ms)*1000000;
    do { if(*p) { --*p; return UACPI_STATUS_OK; } __asm__ volatile("pause"); }
    while(ms && time_counter_ns()<end);
    return UACPI_STATUS_TIMEOUT;
}
void uacpi_kernel_signal_semaphore(uacpi_handle h) { ++*(u32*)h; }
uacpi_thread_id uacpi_kernel_get_thread_id(void) { return (uacpi_thread_id)1; }
uacpi_interrupt_state uacpi_kernel_disable_interrupts(void) {return irq_save();}
void uacpi_kernel_restore_interrupts(uacpi_interrupt_state f) {irq_restore(f);}
uacpi_handle uacpi_kernel_create_spinlock(void) { return uacpi_kernel_alloc(1); }
void uacpi_kernel_free_spinlock(uacpi_handle h) {uacpi_kernel_free(h);}
uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle h) {(void)h;return irq_save();}
void uacpi_kernel_unlock_spinlock(uacpi_handle h,uacpi_cpu_flags f) {(void)h;irq_restore(f);}
uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request* r) {
    (void)r; return UACPI_STATUS_UNIMPLEMENTED;
}
// SCI remains masked on the PIC until Kunix has a general shared IRQ layer.
// Polling still permits pending GPEs and Notify work to execute outside IRQs.
typedef struct {uacpi_interrupt_handler fn; void* ctx; u32 irq;} sci_t;
static sci_t sci;
uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq,
    uacpi_interrupt_handler fn,uacpi_handle ctx,uacpi_handle* out) {
    if(sci.fn || irq>=16 || irq==0 || irq==1 || irq==2 || irq==12)
        return UACPI_STATUS_UNIMPLEMENTED;
    u64 f=irq_save();
    u16 port=irq<8?0x21:0xa1;
    outb(port,inb(port)|(1u<<(irq&7)));
    sci=(sci_t){fn,ctx,irq}; *out=&sci; irq_restore(f);
    return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler fn,uacpi_handle h) {
    (void)h; if(sci.fn!=fn)return UACPI_STATUS_INVALID_ARGUMENT;
    sci.fn=0; return UACPI_STATUS_OK;
}
typedef struct work {struct work* next;uacpi_work_handler fn;void* ctx;} work_t;
static work_t* work_head;
static work_t** work_tail=&work_head;
uacpi_handle uacpi_kernel_create_work_item(void) {return uacpi_kernel_alloc(sizeof(work_t));}
void uacpi_kernel_free_work_item(uacpi_handle h) {uacpi_kernel_free(h);}
void uacpi_kernel_schedule_work(uacpi_work_type t,uacpi_handle h,uacpi_work_handler fn,uacpi_handle ctx) {
    (void)t; work_t* w=h; w->fn=fn;w->ctx=ctx;w->next=0;
    *work_tail=w;work_tail=&w->next;
}
uacpi_status uacpi_kernel_wait_for_work_completion(void) {
    u32 budget=256;
    while(work_head && budget--) {
        work_t* w=work_head; uacpi_work_handler fn=w->fn;void* ctx=w->ctx;
        work_head=w->next; if(!work_head)work_tail=&work_head;
        fn(ctx);
    }
    return work_head?UACPI_STATUS_TIMEOUT:UACPI_STATUS_OK;
}
void battery_firmware_poll(void) {
    if(sci.fn) sci.fn(sci.ctx);
    uacpi_kernel_wait_for_work_completion();
}
// PCI config mechanism 1, segment 0. Widths and offsets are checked before IO.
uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address a,uacpi_handle* out) {
    if(a.segment || a.device>31 || a.function>7)return UACPI_STATUS_NOT_FOUND;
    *out=(void*)(u64)(0x80000000u | a.bus<<16 | a.device<<11 | a.function<<8);
    return UACPI_STATUS_OK;
}
void uacpi_kernel_pci_device_close(uacpi_handle h) {(void)h;}
#define PCI_ACCESS(bits,type,in,out) \
uacpi_status uacpi_kernel_pci_read##bits(uacpi_handle h,uacpi_size off,type* v) { \
 if(off>256-sizeof(*v) || off%sizeof(*v))return UACPI_STATUS_INVALID_ARGUMENT; \
 u64 f=irq_save();outl(0xcf8,(u32)(u64)h | (off&~3u));*v=in(0xcfc+(off&3));irq_restore(f);return UACPI_STATUS_OK;} \
uacpi_status uacpi_kernel_pci_write##bits(uacpi_handle h,uacpi_size off,type v) { \
 if(off>256-sizeof(v) || off%sizeof(v))return UACPI_STATUS_INVALID_ARGUMENT; \
 u64 f=irq_save();outl(0xcf8,(u32)(u64)h | (off&~3u));out(0xcfc+(off&3),v);irq_restore(f);return UACPI_STATUS_OK;}
PCI_ACCESS(8,uacpi_u8,inb,outb)
PCI_ACCESS(16,uacpi_u16,inw,outw)
PCI_ACCESS(32,uacpi_u32,inl,outl)
typedef struct {u32 base,length;} io_range_t;
uacpi_status uacpi_kernel_io_map(uacpi_io_addr base,uacpi_size length,uacpi_handle* out) {
    if(base>65535 || !length || length>65536-base)return UACPI_STATUS_INVALID_ARGUMENT;
    io_range_t* r=uacpi_kernel_alloc(sizeof(*r));if(!r)return UACPI_STATUS_OUT_OF_MEMORY;
    r->base=base;r->length=length;*out=r;return UACPI_STATUS_OK;
}
void uacpi_kernel_io_unmap(uacpi_handle h){uacpi_kernel_free(h);}
#define IO_ACCESS(bits,type,in,out) \
uacpi_status uacpi_kernel_io_read##bits(uacpi_handle h,uacpi_size off,type* v) { \
 io_range_t* r=h;if(off>r->length || sizeof(*v)>r->length-off)return UACPI_STATUS_INVALID_ARGUMENT; \
 *v=in(r->base+off);return UACPI_STATUS_OK;} \
uacpi_status uacpi_kernel_io_write##bits(uacpi_handle h,uacpi_size off,type v) { \
 io_range_t* r=h;if(off>r->length || sizeof(v)>r->length-off)return UACPI_STATUS_INVALID_ARGUMENT; \
 out(r->base+off,v);return UACPI_STATUS_OK;}
IO_ACCESS(8,uacpi_u8,inb,outb)
IO_ACCESS(16,uacpi_u16,inw,outw)
IO_ACCESS(32,uacpi_u32,inl,outl)
