// This file has been generated in whole or in part by a Large Language Model
#define main updater_main
#include "../../system/update.c"
#undef main
#define main installer_main
// The updater names its helpers differently from the installer.
#include "../../compat/primemeridian/tests/install-kunix.c"
#undef main
#include <assert.h>
int main(void) {
 for(unsigned block=512;block<=4096;block*=8) {
  FILE* image=tmpfile(); assert(image); int fd=fileno(image); assert(!ftruncate(fd,512*MiB));
  FILE* kernel=tmpfile(); FILE* loader=tmpfile(); assert(kernel && loader);
  unsigned char payload[4096]={0x7f,'E','L','F'};
  assert(fwrite(payload,1,sizeof(payload),kernel)==sizeof(payload));fflush(kernel);
  assert(fwrite(payload,1,sizeof(payload),loader)==sizeof(payload));fflush(loader);
  Plan p={.block=block};p.part[0].size=512*MiB;
  const char* initial="timeout: 3\n/Kunix\n    protocol: limine\n    kernel_path: boot():/KUNIX.ELF\n    kernel_cmdline: root=PARTUUID=11111111-1111-1111-1111-111111111111 home=PARTUUID=22222222-2222-2222-2222-222222222222\n";
  assert(!fat_write(fd,&p,fileno(kernel),fileno(loader),initial));
  UpdateFat f;assert(!uf_open(&f,fd,512*MiB,block));
  unsigned char hash[32];update_hash("abc",3,hash);
  const unsigned char abc[32]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
  assert(!memcmp(hash,abc,32));
  // Enrollment only adds unreferenced chains; the original boot is intact.
  assert(!uf_prepare(&f));
  char config[4097]={0};assert(!uf_config(&f,config));assert(!strcmp(config,initial));
  unsigned char old[4096];assert(!uf_read_named(&f,0,"KUNIX   ELF",old,sizeof(old)));assert(!memcmp(old,payload,sizeof(old)));
  uint32_t a,b;assert(!uf_slot(&f,0,&a));assert(!uf_slot(&f,1,&b));assert(a!=b);
  unsigned char header[512]={0};memcpy(header,"KXUPD001",8);uset32(header+8,4096);uset32(header+16,UPDATE_ROOT);
  assert(!uf_set_ready(&f,0,header));assert(!uf_set_ready(&f,1,header));
  assert(!uf_activate(&f,0));assert(!uf_config(&f,config));assert(strstr(config,"/KUPDATEA/KERNEL.ELF"));assert(strstr(config,"root=PARTUUID=11111111"));
  assert(strstr(config,"installed=PARTUUID=11111111"));
  assert(!uf_activate(&f,1));assert(!uf_config(&f,config));assert(strstr(config,"kernel_path: boot():/KUPDATEB/KERNEL.ELF") < strstr(config,"kernel_path: boot():/KUPDATEA/KERNEL.ELF"));
  // Reopening validates the on-disk metadata and preserves the baseline chain.
  uf_close(&f);assert(!uf_open(&f,fd,512*MiB,block));assert(!uf_prepare(&f));
  assert(!uf_read_named(&f,0,"KUNIX   ELF",old,sizeof(old)));assert(!memcmp(old,payload,sizeof(old)));
  uf_close(&f);fclose(kernel);fclose(loader);fclose(image);
 }
 puts("update FAT: SHA256, 512/4096 geometry, two slots, activation, fallback preservation passed");
}
