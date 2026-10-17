// This file has been generated in whole or in part by a Large Language Model
// Run the real page cache; substitute only physical frames and file I/O.
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <memory.h>
#include <pagecache.h>
#include <errno.h>
static struct { void* p; unsigned refs; } frames[128];
static unsigned reads, fail_read, large_cache;
static unsigned char bytes[3*4096+13];
uint64_t pmm_count_free(void) { return large_cache ? 4096 : 128; } // 64 or two cache entries
void* kmalloc(uint64_t n) { return calloc(1,n); }
void kfree(void* p) { free(p); }
uint64_t vmm_hhdm(void) { return 0; }
void* pmm_alloc(void) {
 for(unsigned i=0;i<128;i++) if(!frames[i].refs) {
  void* p=NULL; assert(!posix_memalign(&p,4096,4096));
  frames[i].p=p; frames[i].refs=1; return p;
 }
 return NULL;
}
uint32_t pmm_refs(uint64_t p) {
 for(unsigned i=0;i<128;i++) if(frames[i].p==(void*)p) return frames[i].refs;
 return 0;
}
void pmm_ref(uint64_t p) { for(unsigned i=0;i<128;i++) if(frames[i].p==(void*)p) { frames[i].refs++; return; } abort(); }
void pmm_free(uint64_t p) {
 for(unsigned i=0;i<128;i++) if(frames[i].p==(void*)p) {
  assert(frames[i].refs); if(!--frames[i].refs) { free(frames[i].p); frames[i].p=NULL; } return;
 }
 abort();
}
void vfs_getattr(vnode_t* n) { (void)n; }
i64 vfs_read(vnode_t* n,u64 off,void* dst,u64 len) {
 reads++; if(fail_read) return -EIO;
 if(off>=n->size)return 0;
 if(len>n->size-off)len=n->size-off;
 for(u64 i=0;i<len;i++) ((unsigned char*)dst)[i]=large_cache ? (unsigned char)n->id : bytes[off+i];
 return len;
}
int main(void) {
 mount_t mount={0}; vnode_t node={.mnt=&mount,.id=1,.size=sizeof(bytes),.type=VN_REG};
 if (getenv("KUNIX_HOST_LARGE_CACHE")) {
  large_cache=1;
  for(unsigned pass=0;pass<2;pass++) for(unsigned id=1;id<=128;id++) {
   node.id=id;
   i64 frame=pagecache_get(&node,0);
   assert(frame>0 && *(unsigned char*)frame==(unsigned char)id);
   pmm_free(frame);
   assert(pagecache_pages()<=64);
  }
  for(unsigned id=1;id<=128;id++) { node.id=id; pagecache_forget(&node); }
  assert(!pagecache_pages());
  for(unsigned i=0;i<128;i++) assert(!frames[i].refs);
  puts("page cache: hashed sets, colliding files and invalidation passed");
  return 0;
 }
 bytes[0]=17; bytes[4096]=23; bytes[sizeof(bytes)-1]=31;
 i64 a=pagecache_get(&node,0), b=pagecache_get(&node,0);
 assert(a>0 && b==a && reads==1 && pmm_refs(a)==3);
 pmm_free(a); pmm_free(b);
 a=pagecache_get(&node,4096); assert(a>0 && *(unsigned char*)a==23); pmm_free(a);
 a=pagecache_get(&node,8192); assert(a>0); pmm_free(a);
 a=pagecache_get(&node,0); assert(a>0 && reads==4); // LRU page 0 was evicted
 bytes[0]=42; pagecache_forget(&node);
 b=pagecache_get(&node,0); assert(b>0 && b!=a && *(unsigned char*)a==17 && *(unsigned char*)b==42);
 pmm_free(a); pmm_free(b);
 a=pagecache_get(&node,12288); assert(a>0 && ((unsigned char*)a)[12]==31 && ((unsigned char*)a)[13]==0);
 pmm_free(a);
 assert(pagecache_get(&node,16384)==-ENXIO && pagecache_get(&node,1)==-ENXIO);
 pagecache_forget(&node); fail_read=1;
 assert(pagecache_get(&node,0)==-EIO);
 fail_read=0; a=pagecache_get(&node,0); assert(a>0); pmm_free(a);
 pagecache_reclaim(); assert(pagecache_pages()==0);
 for(unsigned i=0;i<128;i++) assert(!frames[i].refs);
 puts("page cache: hits, LRU, invalidation, live references, EOF and I/O errors passed");
 return 0;
}
