// This file has been generated in whole or in part by a Large Language Model
#define main updater_main
#include "../../system/update.c"
#undef main
#define main installer_main
#include "../../compat/primemeridian/tests/install-kunix.c"
#undef main
#include <assert.h>
int main(void){
 for(unsigned block=512;block<=4096;block*=8)for(unsigned scenario=0;scenario<11;scenario++){
  FILE* image=tmpfile(),*kernel=tmpfile(),*loader=tmpfile();assert(image&&kernel&&loader);
  int fd=fileno(image);assert(!ftruncate(fd,512*MiB));unsigned char data[4096]={0x7f,'E','L','F'};
  assert(fwrite(data,1,sizeof data,kernel)==sizeof data);assert(!fflush(kernel));
  assert(fwrite(data,1,sizeof data,loader)==sizeof data);assert(!fflush(loader));
  Plan p={.block=block};p.part[0].size=512*MiB;
  const char* config="timeout: 3\n/Kunix\n kernel_path: boot():/KUNIX.ELF\n kernel_cmdline: root=PARTUUID=11111111-1111-1111-1111-111111111111 home=PARTUUID=22222222-2222-2222-2222-222222222222\n";
  assert(!fat_write(fd,&p,fileno(kernel),fileno(loader),config));UpdateFat f;assert(!uf_open(&f,fd,512*MiB,block));assert(!uf_prepare(&f));
  uint32_t a,b;assert(!uf_slot(&f,0,&a)&&!uf_slot(&f,1,&b));unsigned char ad[4096],bd[4096];
  assert(!UPDATE_IO(fd,ad,4096,uf_at(&f,a),0)&&!UPDATE_IO(fd,bd,4096,uf_at(&f,b),0));
  if(scenario==0){unsigned char* target=uf_find(bd,"KERNEL  ELF");uset16(target+20,uget16(ad+64+20));uset16(target+26,uget16(ad+64+26));assert(!UPDATE_IO(fd,bd,4096,uf_at(&f,b),1));}
  if(scenario==1){unsigned char* target=uf_find(f.rootdir,"KUPDATEB   ");uset16(target+20,a>>16);uset16(target+26,a);}
  if(scenario==2){unsigned char* target=uf_conf_entry(&f),*original=uf_find(f.rootdir,"KUNIX   ELF");uset16(target+20,uget16(original+20));uset16(target+26,uget16(original+26));}
  if(scenario==3){uint32_t c=uf_cluster(uf_find(f.rootdir,"KUNIX   ELF"));uset32(f.fat+4*c,0);}
  if(scenario==4){unsigned char* empty=uf_empty(f.rootdir);assert(empty);memcpy(empty,uf_find(f.rootdir,"KUPDATEA   "),32);}
  if(scenario==5){uf_find(bd,"KERNEL  ELF")[11]=16;assert(!UPDATE_IO(fd,bd,4096,uf_at(&f,b),1));}
  if(scenario==6){uint32_t c=uf_cluster(uf_find(f.rootdir,"EFI        "));uset32(f.fat+4*c,c);}
  if(scenario==7){unsigned char* target=uf_find(f.rootdir,"KUNIX   ELF");uset16(target+20,0xffff);uset16(target+26,0xffff);}
  if(scenario==8)f.rootdir[97]='x';
  if(scenario==9){unsigned char* empty=uf_empty(f.rootdir);assert(empty);memcpy(empty,uf_find(f.rootdir,"KUPDATEA   "),32);empty[0]='k';}
  /* A volume-label attribute must not bypass the update directory type check. */
  if(scenario==10)uf_entry(uf_find(f.rootdir,"KUPDATEA   "),"KUPDATEA   ",8,0,0);
  assert(!UPDATE_IO(fd,f.rootdir,4096,uf_at(&f,2),1));size_t n=(size_t)f.fatsz*block;
  assert(!UPDATE_IO(fd,f.fat,n,32ULL*block,1)&&!UPDATE_IO(fd,f.fat,n,(32ULL+f.fatsz)*block,1));uf_close(&f);
  int accepted=uf_open(&f,fd,512*MiB,block)==0;
  if(accepted){fprintf(stderr,"FAIL ownership accepted scenario %u block %u\n",scenario,block);uf_close(&f);return 1;}
  fclose(image);fclose(kernel);fclose(loader);
 }
 puts("update ownership: crosslinks, free references, duplicate names, invalid attributes/chains rejected on 512/4096 ESPs");
}
