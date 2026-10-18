// This file has been generated in whole or in part by a Large Language Model
#include <stddef.h>
#include <stdint.h>
static int update_fault(int,void*,size_t,uint64_t,int);
#define UPDATE_IO update_fault
#define main updater_main
#include "../../system/update.c"
#undef main
static long write_budget=-1;
static int update_fault(int fd,void* b,size_t n,uint64_t at,int writing){if(writing&&write_budget>=0){if(!write_budget){errno=EIO;return -1;}write_budget--;}return uio_real(fd,b,n,at,writing);}
#define main installer_main
#include "../../compat/primemeridian/tests/install-kunix.c"
#undef main
#include <assert.h>
/* A clean directory-write failure during first activation must leave the
 * original menu readable, rather than a new menu cut to the old file size. */
static void first_activation_failures(unsigned block){
 for(long trial=0;trial<3;trial++){
  FILE* image=tmpfile(),*kernel=tmpfile(),*loader=tmpfile();assert(image&&kernel&&loader);
  int fd=fileno(image);assert(!ftruncate(fd,512*MiB));unsigned char payload[4096]={0x7f,'E','L','F'};
  assert(fwrite(payload,1,sizeof payload,kernel)==sizeof payload);assert(!fflush(kernel));
  assert(fwrite(payload,1,sizeof payload,loader)==sizeof payload);assert(!fflush(loader));
  Plan p={.block=block};p.part[0].size=512*MiB;
  const char* initial="timeout: 3\n/Kunix original\n protocol: limine\n kernel_path: boot():/KUNIX.ELF\n kernel_cmdline: root=PARTUUID=11111111-1111-1111-1111-111111111111 home=PARTUUID=22222222-2222-2222-2222-222222222222\n";
  assert(!fat_write(fd,&p,fileno(kernel),fileno(loader),initial));UpdateFat f;
  assert(!uf_open(&f,fd,512*MiB,block)&&!uf_prepare(&f));
  write_budget=trial;int result=uf_activate(&f,0);write_budget=-1;
  assert(result<0);uf_close(&f);
  assert(!uf_open(&f,fd,512*MiB,block));char actual[4097];assert(!uf_config(&f,actual));
  if(strcmp(actual,initial)){fprintf(stderr,"FAIL first activation changed/truncated original menu after write failure %ld block %u\n",trial,block);abort();}
  uf_close(&f);fclose(loader);fclose(kernel);fclose(image);
 }
}
int main(void){
 first_activation_failures(512);first_activation_failures(4096);
 FILE* image=tmpfile(),*kernel=tmpfile(),*loader=tmpfile(),*source=tmpfile();assert(image&&kernel&&loader&&source);
 int fd=fileno(image);assert(!ftruncate(fd,512*MiB));assert(!ftruncate(fileno(source),UPDATE_ROOT));
 unsigned char payload[4096]={0x7f,'E','L','F'};assert(fwrite(payload,1,4096,kernel)==4096&&fwrite(payload,1,4096,loader)==4096);fflush(kernel);fflush(loader);
 Plan p={.block=512};p.part[0].size=512*MiB;
 const char* config="timeout: 3\n/Kunix\n kernel_cmdline: root=PARTUUID=11111111-1111-1111-1111-111111111111 home=PARTUUID=22222222-2222-2222-2222-222222222222\n";
 assert(!fat_write(fd,&p,fileno(kernel),fileno(loader),config));UpdateFat f;assert(!uf_open(&f,fd,512*MiB,512));assert(!uf_prepare(&f));
 unsigned char h[512]={0};memcpy(h,"KXUPD001",8);uset32(h+8,4096);uset32(h+16,UPDATE_ROOT);update_hash(payload,4096,h+24);
 UpdateHash hs;uh_init(&hs);unsigned char zero[4096]={0};for(unsigned i=0;i<UPDATE_ROOT/4096;i++)uh_add(&hs,zero,4096);uh_done(&hs,h+56);
 uint32_t a,b;assert(!uf_slot(&f,0,&a)&&!uf_slot(&f,1,&b));
 assert(!uf_copy(&f,a,"KERNEL  ELF",fileno(kernel),0,4096,h+24,1));assert(!uf_copy(&f,a,"ROOT    IMG",fileno(source),0,UPDATE_ROOT,h+56,1));assert(!uf_set_ready(&f,0,h));assert(!uf_activate(&f,0));assert(!uf_verify_slot(&f,0));
 for(unsigned trial=0;trial<4;trial++){
  assert(!uf_set_ready(&f,1,NULL));write_budget=(long[]){0,1,16384,32767}[trial];
  assert(uf_copy(&f,b,"ROOT    IMG",fileno(source),0,UPDATE_ROOT,h+56,1)<0);write_budget=-1;
  unsigned char ready[512];assert(uf_ready(&f,1,ready)<0);assert(uf_verify_slot(&f,1)<0);
  char current[4097];assert(!uf_config(&f,current)&&uf_active(current)==0);
  assert(!uf_verify_slot(&f,0));unsigned char original[4096];assert(!uf_read_named(&f,0,"KUNIX   ELF",original,4096)&&!memcmp(original,payload,4096));
 }
 // A complete ready marker cannot conceal changed inactive payload bytes.
 assert(!uf_copy(&f,b,"KERNEL  ELF",fileno(kernel),0,4096,h+24,1));assert(!uf_copy(&f,b,"ROOT    IMG",fileno(source),0,UPDATE_ROOT,h+56,1));assert(!uf_set_ready(&f,1,h));assert(!uf_verify_slot(&f,1));
 unsigned char d[4096];assert(!uio_real(fd,d,4096,uf_at(&f,b),0));uint32_t c=uf_cluster(uf_find(d,"ROOT    IMG"));unsigned char damage=1;assert(!uio_real(fd,&damage,1,uf_at(&f,c),1));assert(uf_verify_slot(&f,1)<0);assert(!uf_verify_slot(&f,0));
 uf_close(&f);fclose(source);fclose(loader);fclose(kernel);fclose(image);
 puts("update interruption: early/middle/late staging failure retains selected and original; corrupt rollback refused");
}
