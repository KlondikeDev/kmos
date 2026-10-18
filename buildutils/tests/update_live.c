// This file has been generated in whole or in part by a Large Language Model
#define main updater_main
#include "../../system/update.c"
#undef main
#include <assert.h>
int main(void){
 FILE* k=tmpfile(),*r=tmpfile();assert(k&&r);
 unsigned char elf[4096]={0x7f,'E','L','F',2,1};uset16(elf+18,62);assert(fwrite(elf,1,4096,k)==4096);fflush(k);
 assert(!ftruncate(fileno(r),UPDATE_ROOT));unsigned char sb[1376]={0};uset32(sb+1372,0x19540119);sb[209]=1;assert(!uio_real(fileno(r),sb,sizeof(sb),8192,1));
 unsigned char header[512];assert(!up_live_header(fileno(k),fileno(r),header));assert(uget64(header+8)==4096&&uget64(header+16)==UPDATE_ROOT);
 unsigned char expected[32];memcpy(expected,header+56,32);unsigned char corrupt=1;assert(!uio_real(fileno(r),&corrupt,1,100,1));assert(uh_file(fileno(r),0,UPDATE_ROOT,expected)<0);
 assert(!ftruncate(fileno(r),1));assert(up_live_header(fileno(k),fileno(r),header)<0);
 fclose(r);fclose(k);puts("live update: checked pristine source bundle and corruption/short source refusal passed");
}
