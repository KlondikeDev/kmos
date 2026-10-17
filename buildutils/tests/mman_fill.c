// This file has been generated in whole or in part by a Large Language Model
// Exercise the production fault filler with real uncached file reads.
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "../../kernel/mman.c"
static u64 mapped, mapped_flags;
static struct {u64 va, frame, flags;} entries[32];
static unsigned entry_count;
static unsigned cow_calls;
static unsigned char contents[8192];
uint64_t pmm_count_free(void) { return 0; } // page-cache metadata cannot be reserved
void* kmalloc(uint64_t n) { return calloc(1,n); }
void kfree(void* p) { free(p); }
uint64_t vmm_hhdm(void) { return 0; }
void* pmm_alloc(void) { void* p=NULL; assert(!posix_memalign(&p,4096,4096)); return p; }
void pmm_free(uint64_t p) { free((void*)p); }
void pmm_ref(uint64_t p) { (void)p; abort(); }
uint32_t pmm_refs(uint64_t p) { (void)p; return 1; }
u64 fb_cache_flags(u64 p) { (void)p; return 0; }
bool vmm_map_in(uint64_t s,uint64_t va,uint64_t pa,uint64_t flags) {
 (void)s; mapped=pa; mapped_flags=flags;
 unsigned i=0; while(i<entry_count && entries[i].va!=va) i++;
 assert(i<32); if(i==entry_count) entry_count++;
 entries[i].va=va; entries[i].frame=pa; entries[i].flags=flags; return true;
}
uint64_t vmm_get_pte(uint64_t s,uint64_t va) {
 (void)s; for(unsigned i=0;i<entry_count;i++) if(entries[i].va==va) return entries[i].frame|entries[i].flags;
 return 0;
}
void vmm_unmap_in(uint64_t s,uint64_t va) { (void)s; (void)va; mapped=0; }
bool vmm_break_cow(uint64_t s,uint64_t va) { (void)s; (void)va; cow_calls++; return false; }
bool user_map(process_t* p,u64 va,u32 pf) {
 (void)pf; u64 frame=(u64)pmm_alloc(); memset((void*)frame,0,4096);
 p->user_pages++; return vmm_map_in(p->space,va,frame,PTE_PRESENT|PTE_WRITE);
}
void vfs_getattr(vnode_t* n) { (void)n; }
i64 vfs_read(vnode_t* n,u64 off,void* dst,u64 len) {
 if(off>=n->size) return 0;
 if(len>n->size-off) len=n->size-off;
 memcpy(dst,contents+off,len); return len;
}
int main(void) {
 vnode_t node={.id=1,.size=sizeof(contents),.type=VN_REG};
 file_t file={.node=&node};
 vm_backing_t* b=calloc(1,sizeof(*b)+2*sizeof(elf64_phdr_t)); b->file=&file;
 process_t* p=calloc(1,sizeof(*p));
 vma_t v={.start=0x400000,.end=0x401000,.prot=PROT_READ|PROT_WRITE|PROT_EXEC,.backing=b};
 memset(contents,0x6a,sizeof(contents));
 assert(fill_page(p,v.start,&v,true));
 assert((mapped_flags&PTE_WRITE) && !cow_calls && *(u8*)mapped==0x6a);
 pmm_free(mapped); p->user_pages=0;
 b->count=2;
 b->headers[0]=(elf64_phdr_t){.p_type=PT_LOAD,.p_vaddr=v.start,.p_filesz=4096,.p_memsz=4096};
 b->headers[1]=(elf64_phdr_t){.p_type=PT_LOAD,.p_vaddr=v.start+128,.p_offset=4096,.p_filesz=16,.p_memsz=64};
 memset(contents+4096,0x3b,16);
 assert(fill_page(p,v.start,&v,false));
 unsigned char* data=(void*)mapped;
 assert(data[127]==0x6a && data[128]==0x3b && data[143]==0x3b);
 for(unsigned i=144;i<192;i++) assert(data[i]==0);
 assert(data[192]==0x6a);
 pmm_free(mapped); free(b); free(p);
 puts("fault filler: uncached first write and overlapping ELF BSS passed");
}
