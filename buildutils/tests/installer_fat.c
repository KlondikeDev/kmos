// This file has been generated in whole or in part by a Large Language Model
#define main installer_main
#include "../../compat/primemeridian/tests/install-kunix.c"
#undef main
#include <assert.h>
#include <gpt.h>
static int gpt_fd;
static uint64_t gpt_bytes;
static unsigned gpt_block;
void* kmalloc(uint64_t n) { return malloc(n); }
void kfree(void* p) { free(p); }
u64 disk_sectors(u8 d) { (void)d; return gpt_bytes/512; }
u32 disk_block_size(u8 d) { (void)d; return gpt_block; }
bool disk_read(u8 d,u64 at,u32 n,void* buf) { (void)d; return !transfer(gpt_fd,buf,(size_t)n*512,at*512,0); }
int main(void) {
 for(unsigned block=512;block<=4096;block*=8) {
  struct kdisk_info info={.sectors=8ULL*1024*1024,.block_size=block,.flags=KDISK_WHOLE|KDISK_NVME};
  Plan p; assert(make_plan(&info,&p)==0); assert(p.part[0].start==MiB && p.part[0].size==512*MiB);
  assert(p.part[1].start%MiB==0 && p.part[2].start%MiB==0);
  assert(p.part[2].start+p.part[2].size<=p.bytes-16384-block);
  info.sectors=2*1024*1024; assert(make_plan(&info,&p)<0);
  info.sectors=8ULL*1024*1024; assert(make_plan(&info,&p)==0);
  assert(!plan_guids(&p));
  FILE* disk=tmpfile(); assert(disk); gpt_fd=fileno(disk); gpt_bytes=p.bytes; gpt_block=block;
  assert(!ftruncate(gpt_fd,p.bytes)); assert(!write_gpt(gpt_fd,&p));
  gpt_partition_t parts[128]; assert(gpt_scan(8,parts,128)==3);
  for(unsigned j=0;j<3;j++) { assert(parts[j].start==p.part[j].start/512 && parts[j].count==p.part[j].size/512); assert(!memcmp(parts[j].guid,p.part[j].guid,16)); }
  fclose(disk);
  p.part[0].start=0;
  char path[128]; snprintf(path,sizeof(path),"/tmp/kunix-installer-fat-%u.img",block);
  int image=open(path,O_RDWR|O_CREAT|O_TRUNC,0600); assert(image>=0); assert(!ftruncate(image,512*MiB));
  FILE* kernel=tmpfile(); FILE* loader=tmpfile(); assert(kernel && loader);
  unsigned char payload[10001]; for(unsigned i=0;i<sizeof(payload);i++) payload[i]=i;
  assert(fwrite(payload,1,sizeof(payload),kernel)==sizeof(payload)); fflush(kernel);
  assert(fwrite(payload,1,sizeof(payload),loader)==sizeof(payload)); fflush(loader);
  assert(!fat_write(image,&p,fileno(kernel),fileno(loader),"timeout: 3\n/Kunix\n    protocol: limine\n"));
  close(image); fclose(kernel); fclose(loader);
  char command[400]; snprintf(command,sizeof(command),"/usr/sbin/fsck.fat -vn %s",path); assert(system(command)==0);
  char extracted[128]; snprintf(extracted,sizeof(extracted),"/tmp/kunix-installer-config-%u.txt",block);
  snprintf(command,sizeof(command),"mcopy -i %s ::limine.conf %s",path,extracted); assert(!system(command));
  FILE* config=fopen(extracted,"rb"); assert(config); char configtext[80]={0}; assert(fread(configtext,1,sizeof(configtext)-1,config)>0); assert(strstr(configtext,"timeout: 3\n")); fclose(config); unlink(extracted);
  snprintf(command,sizeof(command),"mcopy -i %s ::EFI/BOOT/BOOTX64.EFI %s",path,extracted); assert(!system(command));
  config=fopen(extracted,"rb"); assert(config); unsigned char actual[sizeof(payload)]; assert(fread(actual,1,sizeof(actual),config)==sizeof(payload)); assert(!memcmp(actual,payload,sizeof(payload))); fclose(config); unlink(extracted); unlink(path);
 }
 puts("installer: GPT layout and FAT32 512/4096 images passed fsck.fat");
 return 0;
}
