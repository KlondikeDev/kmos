// This file has been generated in whole or in part by a Large Language Model
// Sparse fixtures exercise the production parser without allocating a whole disk.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <gpt.h>
#include <errno.h>
static u8 mbr[4096], primary[4096], backup[4096], entries[16384], other[16384];
static u32 block;
static u64 blocks;
static int io_fail, alloc_fail, alloc_calls, outstanding;
void* kmalloc(u64 n) {
 if(++alloc_calls==alloc_fail) return NULL;
 void* p=malloc(n); if(p) outstanding++; return p;
}
void kfree(void* p) { if(p) outstanding--; free(p); }
u64 disk_sectors(u8 disk) { return disk == 8 ? blocks*(block/512) : 0; }
u32 disk_block_size(u8 disk) { (void)disk; return block; }
bool disk_read(u8 disk,u64 lba,u32 n,void* out) {
 if(io_fail || disk!=8 || lba>disk_sectors(disk) || n>disk_sectors(disk)-lba) return false;
 u8* dst=out;
 for(u32 i=0;i<n;i++) {
  u64 pos=(lba+i)*512, end=blocks*block;
  const u8* src=NULL;
  if(pos<block) src=mbr+pos;
  else if(pos<2*block) src=primary+pos-block;
  else if(pos<2*block+sizeof(entries)) src=entries+pos-2*block;
  else if(pos>=end-block) src=backup+pos-(end-block);
  else if(pos>=end-block-sizeof(other)) src=other+pos-(end-block-sizeof(other));
  if(src) memcpy(dst+i*512,src,512); else memset(dst+i*512,0,512);
 }
 return true;
}
static void put32(u8* p,u32 n) { for(int i=0;i<4;i++) p[i]=n>>(8*i); }
static void put64(u8* p,u64 n) { for(int i=0;i<8;i++) p[i]=n>>(8*i); }
static u32 crc(const u8* p,u32 n) {
 u32 c=~0U; for(u32 i=0;i<n;i++) { c^=p[i]; for(int j=0;j<8;j++) c=(c>>1)^((0-(c&1))&0xedb88320U); } return ~c;
}
static void header_crc(u8* h) { put32(h+16,0); put32(h+16,crc(h,92)); }
static void refresh(void) {
 put32(primary+88,crc(entries,sizeof(entries))); header_crc(primary);
 put32(backup+88,crc(other,sizeof(other))); header_crc(backup);
}
static void entry(unsigned slot,u64 start,u64 end,u8 guid) {
 u8* p=entries+slot*128; p[0]=1; p[16]=guid; put64(p+32,start); put64(p+40,end);
}
static void fixture(u32 size) {
 block=size; blocks=100000; io_fail=0;
 memset(mbr,0,sizeof(mbr)); memset(primary,0,sizeof(primary)); memset(backup,0,sizeof(backup)); memset(entries,0,sizeof(entries));
 mbr[510]=0x55; mbr[511]=0xaa; mbr[450]=0xee; put32(mbr+454,1); put32(mbr+458,blocks-1);
 memcpy(primary,"EFI PART",8); put32(primary+8,0x10000); put32(primary+12,92);
 put64(primary+24,1); put64(primary+32,blocks-1); put64(primary+40,64); put64(primary+48,blocks-64); primary[56]=5;
 put64(primary+72,2); put32(primary+80,128); put32(primary+84,128);
 memcpy(backup,primary,92); put64(backup+24,blocks-1); put64(backup+32,1); put64(backup+72,blocks-1-sizeof(entries)/block);
 entry(2,100,199,7); memcpy(other,entries,sizeof(entries)); refresh();
}
static void invalid(void) {
 gpt_partition_t out[128], original[128]; memset(out,0x5a,sizeof(out)); memcpy(original,out,sizeof(out));
 assert(gpt_scan(8,out,128)<0); assert(!memcmp(out,original,sizeof(out)));
}
int main(void) {
 gpt_partition_t out[128]; u8 guid[16]; char text[37];
 assert(crc((const u8*)"123456789",9)==0xcbf43926U);
 assert(gpt_guid_parse("00112233-4455-6677-8899-aabbccddeeff",guid));
 assert(guid[0]==0x33 && guid[4]==0x55 && guid[6]==0x77 && guid[8]==0x88);
 gpt_guid_format(guid,text); assert(!strcmp(text,"00112233-4455-6677-8899-aabbccddeeff"));
 assert(!gpt_guid_parse("00112233-4455-6677-8899-aabbccddeefz",guid));
 assert(!gpt_guid_parse("",guid));
 for(unsigned size=512;size<=4096;size*=8) {
  fixture(size); assert(gpt_scan(8,out,128)==1); assert(out[0].slot==3 && out[0].parent==8 && out[0].start==100*(size/512) && out[0].count==100*(size/512));
  primary[16]^=1; assert(gpt_scan(8,out,128)==1); backup[16]^=1; invalid();
  fixture(size); entries[8]^=1; assert(gpt_scan(8,out,128)==1);
  fixture(size); backup[56]^=1; header_crc(backup); invalid();
  fixture(size); other[2*128+16]=9; refresh(); invalid();
  fixture(size); primary[0]=backup[0]='X'; header_crc(primary); header_crc(backup); invalid();
  fixture(size); put32(primary+8,0x20000); put32(backup+8,0x20000); refresh(); invalid();
  fixture(size); put32(primary+12,block+1); put32(backup+12,block+1); invalid();
  fixture(size); put64(primary+24,2); put64(backup+24,blocks-2); refresh(); invalid();
  fixture(size); put64(primary+32,blocks-2); put64(backup+32,2); refresh(); invalid();
  fixture(size); entry(2,100,99,7); memcpy(other,entries,sizeof(entries)); refresh(); invalid();
  fixture(size); entry(2,100,199,0); memcpy(other,entries,sizeof(entries)); refresh(); invalid();
  fixture(size); entry(3,150,249,8); memcpy(other,entries,sizeof(entries)); refresh(); invalid();
  fixture(size); entry(3,300,399,7); memcpy(other,entries,sizeof(entries)); refresh(); invalid();
  fixture(size); entry(3,blocks-50,blocks-1,8); memcpy(other,entries,sizeof(entries)); refresh(); invalid();
  fixture(size); entry(3,~0ULL-1,~0ULL,8); memcpy(other,entries,sizeof(entries)); refresh(); invalid();
  fixture(size); put64(primary+72,~0ULL); put64(backup+72,~0ULL); refresh(); invalid();
  fixture(size); put32(primary+80,129); put32(backup+80,129); refresh(); invalid();
  fixture(size); put32(primary+84,0xffffffff); put32(backup+84,0xffffffff); refresh(); invalid();
  fixture(size); put64(primary+40,2); put64(backup+40,2); refresh(); invalid();
  fixture(size); mbr[450]=0x83; invalid();
  fixture(size); memset(entries,0,sizeof(entries)); memset(other,0,sizeof(other));
  put32(primary+80,1); put32(backup+80,1);
  put64(primary+40,3); put64(backup+40,3);
  put64(primary+48,blocks-3); put64(backup+48,blocks-3); put64(backup+72,blocks-2);
  put32(primary+88,crc(entries,128)); put32(backup+88,crc(other,128)); header_crc(primary); header_crc(backup); invalid();
  fixture(size); io_fail=1; invalid();
  fixture(size); assert(gpt_scan(8,out,0)==-ENOSPC);
  for(int failure=1;failure<=5;failure++) {
   fixture(size); alloc_calls=0; alloc_fail=failure;
   assert(gpt_scan(8,out,128)==-ENOMEM); assert(!outstanding); alloc_fail=0;
  }
  fixture(size); entry(3,200,299,8); memcpy(other,entries,sizeof(entries)); refresh(); assert(gpt_scan(8,out,128)==2);
  fixture(size); memset(entries,0,sizeof(entries)); memcpy(other,entries,sizeof(entries)); refresh(); assert(gpt_scan(8,NULL,0)==0);
  fixture(size); memset(entries,0,sizeof(entries));
  for(unsigned i=0;i<128;i++) entry(i,100+2*i,101+2*i,i+1);
  memcpy(other,entries,sizeof(entries)); refresh(); assert(gpt_scan(8,out,128)==128);
  memset(out,0x5a,sizeof(out)); assert(gpt_scan(8,out,127)==-ENOSPC); assert(out[0].parent==0x5a);
  assert(!outstanding);
 }
 puts("gpt: native 512/4096, GUIDs, redundant tables and malformed metadata passed");
 return 0;
}
