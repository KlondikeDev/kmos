// This file has been generated in whole or in part by a Large Language Model
// Stage an inactive EFI generation; never overwrite the installed UFS root.
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "../include/disk_ioctl.h"
#define UPDATE_ROOT (128U*1024*1024)
#define UPDATE_KERNEL (4U*1024*1024)
#define UPDATE_CLUSTER 4096U
#define UPDATE_EOC 0x0fffffffU

typedef struct {uint32_t h[8];uint64_t bytes;unsigned used;unsigned char buf[64];} UpdateHash;
static uint32_t uror(uint32_t a,unsigned n){return a>>n|a<<(32-n);}
static void uh_block(UpdateHash* s,const unsigned char* p){
 static const uint32_t k[64]={
 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
 0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
 0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
 0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
 0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
 uint32_t w[64];for(unsigned i=0;i<16;i++)w[i]=(uint32_t)p[4*i]<<24|(uint32_t)p[4*i+1]<<16|(uint32_t)p[4*i+2]<<8|p[4*i+3];
 for(unsigned i=16;i<64;i++){uint32_t a=w[i-15],b=w[i-2];w[i]=w[i-16]+(uror(a,7)^uror(a,18)^(a>>3))+w[i-7]+(uror(b,17)^uror(b,19)^(b>>10));}
 uint32_t a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7];
 for(unsigned i=0;i<64;i++){uint32_t x=h+(uror(e,6)^uror(e,11)^uror(e,25))+((e&f)^(~e&g))+k[i]+w[i],y=(uror(a,2)^uror(a,13)^uror(a,22))+((a&b)^(a&c)^(b&c));h=g;g=f;f=e;e=d+x;d=c;c=b;b=a;a=x+y;}
 s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
}
static void uh_init(UpdateHash* s){*s=(UpdateHash){.h={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};}
static void uh_add(UpdateHash* s,const void* data,size_t n){const unsigned char* p=data;s->bytes+=n;while(n){size_t k=64-s->used;if(k>n)k=n;memcpy(s->buf+s->used,p,k);s->used+=k;p+=k;n-=k;if(s->used==64){uh_block(s,s->buf);s->used=0;}}}
static void uh_done(UpdateHash* s,unsigned char hash[32]){uint64_t bits=s->bytes*8;unsigned char pad[128]={0x80};unsigned n=s->used<56?56-s->used:120-s->used;uh_add(s,pad,n);for(unsigned i=0;i<8;i++)pad[i]=bits>>(56-i*8);uh_add(s,pad,8);for(unsigned i=0;i<32;i++)hash[i]=s->h[i/4]>>(24-(i%4)*8);}
static __attribute__((unused)) void update_hash(const void* p,size_t n,unsigned char hash[32]){UpdateHash s;uh_init(&s);uh_add(&s,p,n);uh_done(&s,hash);}
static unsigned uget16(const unsigned char* p){return p[0]|p[1]<<8;}
static uint32_t uget32(const unsigned char* p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t uget64(const unsigned char* p){return (uint64_t)uget32(p)|(uint64_t)uget32(p+4)<<32;}
static void uset16(unsigned char* p,unsigned n){p[0]=n;p[1]=n>>8;}
static void uset32(unsigned char* p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=n>>(i*8);}
// Host tests can inject failure at every I/O boundary without production hooks.
#ifndef UPDATE_IO
#define UPDATE_IO uio_real
#endif
static int uio_real(int fd,void* buf,size_t n,uint64_t at,int writing){unsigned char* p=buf;while(n){ssize_t r=writing?pwrite(fd,p,n,at):pread(fd,p,n,at);if(r<0&&errno==EINTR)continue;if(r<=0){if(!r)errno=EIO;return -1;}p+=r;n-=r;at+=r;}return 0;}
typedef struct {int fd;uint64_t bytes,data;unsigned block,spc;uint32_t fatsz,clusters,root;unsigned char* fat;unsigned char rootdir[4096];} UpdateFat;
static uint64_t uf_at(UpdateFat* f,uint32_t c){return f->data+(uint64_t)(c-2)*4096;}
static int uf_valid(UpdateFat* f,uint32_t c){return c>=2&&c<f->clusters+2;}
static uint32_t uf_next(UpdateFat* f,uint32_t c){return uget32(f->fat+4*c)&0x0fffffff;}
static void uf_close(UpdateFat* f){free(f->fat);f->fat=NULL;}
/* Validate ownership before staging writes. The installer and update writer
 * support single-cluster directories; reject larger ones instead of checking
 * only their first cluster. Interrupted enrollment may leave allocated orphan
 * clusters: retain those, but require every reachable cluster to have one owner. */
static int uf_ownership(UpdateFat* f){
 struct {uint32_t cluster,parent;} dirs[256];unsigned pending=1;
 unsigned char* used=calloc((size_t)f->clusters+2,1);if(!used)return -1;
 dirs[0].cluster=f->root;dirs[0].parent=0;int result=-1;
 for(unsigned which=0;which<pending;which++){
  uint32_t dc=dirs[which].cluster,parent=dirs[which].parent;
  if(!uf_valid(f,dc)||used[dc]||uf_next(f,dc)<0x0ffffff8)goto done;
  used[dc]=1;unsigned char dir[4096];
  if(UPDATE_IO(f->fd,dir,sizeof dir,uf_at(f,dc),0))goto done;
  int ended=0;
  for(unsigned offset=0;offset<sizeof dir;offset+=32){
   unsigned char* e=dir+offset;
   if(!e[0]){ended=1;continue;}
   if(ended)goto done;
   if(e[0]==0xe5)continue;
   if(e[11]==15){
    /* The fixed ESP has exactly one long alias: limine.conf. Reject other
     * aliases rather than letting a long boot name shadow a checked short
     * entry. The supported alias must match its short entry and checksum. */
    static const unsigned positions[]={1,3,5,7,9,14,16,18,20,22,24,28,30};
    static const unsigned char alias[]="LIMINE~1CON",name[]="limine.conf";
    if(dc!=f->root||offset+64>sizeof dir||e[0]!=0x41||e[12]||uget16(e+26)||memcmp(e+32,alias,11)||(e[43]&0x18))goto done;
    unsigned char sum=0;for(unsigned j=0;j<11;j++)sum=((sum&1)<<7)+(sum>>1)+alias[j];
    if(e[13]!=sum)goto done;
    for(unsigned j=0;j<13;j++)if(uget16(e+positions[j])!=(j<11?name[j]:j==11?0:0xffff))goto done;
    continue;
   }
   unsigned attr=e[11];uint32_t c=uget16(e+26)|(uint32_t)uget16(e+20)<<16;
   uint32_t bytes=uget32(e+28);
   if((attr&0xc0)||((attr&0x18)==0x18))goto done;
   /* FAT aliases are case insensitive, even though generated names are upper
    * case. Duplicate aliases must not select different boot payloads. */
   for(unsigned previous=0;previous<offset;previous+=32){
    unsigned char* old=dir+previous;if(old[0]==0xe5||old[11]==15)continue;
    unsigned j;for(j=0;j<11;j++){unsigned a=e[j],b=old[j];if(a>='a'&&a<='z')a-=32;if(b>='a'&&b<='z')b-=32;if(a!=b)break;}
    if(j==11)goto done;
   }
   if(!memcmp(e,".          ",11)||!memcmp(e,"..         ",11)){
    uint32_t expected=e[1]=='.'?parent:dc;
    if(dc==f->root||attr!=16||bytes||(c!=expected&&!(expected==f->root&&c==0)))goto done;
    continue;
   }
   if(attr&8){if(dc!=f->root||c||bytes)goto done;continue;}
   if(dc==f->root&&(!memcmp(e,"KUPDATEA   ",11)||!memcmp(e,"KUPDATEB   ",11))&&attr!=16)goto done;
   if((!memcmp(e,"KERNEL  ELF",11)||!memcmp(e,"ROOT    IMG",11)||
       (dc==f->root&&(!memcmp(e,"LIMINE~1CON",11)||!memcmp(e,"KUNIX   ELF",11))))&&(attr&16))goto done;
   if(attr&16){
    if(bytes||pending==sizeof dirs/sizeof dirs[0])goto done;
    dirs[pending].cluster=c;dirs[pending++].parent=dc;
    continue;
   }
   if(!bytes){if(c)goto done;continue;}
   uint64_t count=((uint64_t)bytes+4095)/4096;
   if(count>f->clusters)goto done;
   for(uint64_t i=0;i<count;i++){
    if(!uf_valid(f,c)||used[c])goto done;
    used[c]=1;uint32_t next=uf_next(f,c);
    if(i+1==count){if(next<0x0ffffff8)goto done;}
    else if(!uf_valid(f,next))goto done;
    c=next;
   }
  }
 }
 result=0;
 done:free(used);return result;
}
static int uf_open(UpdateFat* f,int fd,uint64_t bytes,unsigned block){
 memset(f,0,sizeof(*f));f->fd=fd;f->bytes=bytes;f->block=block;
 unsigned char b[4096];if((block!=512&&block!=4096)||UPDATE_IO(fd,b,block,0,0))return -1;
 // This deliberately supports only the installer's fixed 512MiB FAT32 ESP.
 if(bytes!=512ULL*1024*1024||uget16(b+11)!=block||b[13]*block!=4096||uget16(b+14)!=32||b[16]!=2||uget16(b+17)||uget16(b+22)||uget16(b+40)||memcmp(b+71,"KUNIX ESP  ",11)||b[510]!=0x55||b[511]!=0xaa)return -1;
 f->spc=b[13];f->fatsz=uget32(b+36);f->root=uget32(b+44);
 if(f->root!=2||!f->fatsz||f->fatsz>2048||uget32(b+32)!=(bytes/block))return -1;
 f->data=(32ULL+2ULL*f->fatsz)*block;if(f->data>=bytes)return -1;
 f->clusters=(bytes-f->data)/4096;if((uint64_t)f->fatsz*block/4<f->clusters+2)return -1;
 size_t n=(size_t)f->fatsz*block;f->fat=malloc(n);unsigned char* other=malloc(n);if(!f->fat||!other){free(other);uf_close(f);return -1;}
 int bad=UPDATE_IO(fd,f->fat,n,32ULL*block,0)||UPDATE_IO(fd,other,n,(32ULL+f->fatsz)*block,0)||memcmp(f->fat,other,n);free(other);
 if(bad||uf_next(f,2)<0x0ffffff8||UPDATE_IO(fd,f->rootdir,4096,uf_at(f,2),0)||uf_ownership(f)){uf_close(f);return -1;}return 0;
}
static unsigned char* uf_find(unsigned char dir[4096],const char name[11]){for(unsigned i=0;i<4096;i+=32){if(!dir[i])break;if(dir[i]!=0xe5&&dir[i+11]!=15&&!memcmp(dir+i,name,11))return dir+i;}return NULL;}
static uint32_t uf_cluster(unsigned char* e){return uget16(e+26)|(uint32_t)uget16(e+20)<<16;}
static void uf_entry(unsigned char* e,const char name[11],unsigned attr,uint32_t c,uint32_t size){memset(e,0,32);memcpy(e,name,11);e[11]=attr;uset16(e+20,c>>16);uset16(e+26,c);uset32(e+28,size);uset16(e+16,0x21);uset16(e+18,0x21);uset16(e+24,0x21);}
static unsigned char* uf_empty(unsigned char dir[4096]){for(unsigned i=0;i<4096-32;i+=32)if(!dir[i]||dir[i]==0xe5)return dir+i;return NULL;}
static int uf_chain(UpdateFat* f,uint32_t c,uint32_t bytes){uint32_t count=(bytes+4095)/4096;for(unsigned i=0;i<count;i++){if(!uf_valid(f,c))return -1;uint32_t next=uf_next(f,c);if(i+1==count)return next>=0x0ffffff8?0:-1;c=next;}return -1;}
static uint32_t uf_alloc(UpdateFat* f,uint32_t bytes){uint32_t n=(bytes+4095)/4096,start=0,run=0;for(uint32_t c=2;c<f->clusters+2;c++){if(!uf_next(f,c)){if(!run)start=c;if(++run==n){for(uint32_t j=0;j<n;j++)uset32(f->fat+4*(start+j),j+1==n?UPDATE_EOC:start+j+1);return start;}}else run=0;}return 0;}
static __attribute__((unused)) int uf_read_named(UpdateFat* f,uint32_t dir,const char name[11],void* out,size_t n){unsigned char d[4096];if(dir){if(!uf_valid(f,dir)||UPDATE_IO(f->fd,d,4096,uf_at(f,dir),0))return -1;}else memcpy(d,f->rootdir,4096);unsigned char* e=uf_find(d,name);if(!e||n>uget32(e+28))return -1;uint32_t c=uf_cluster(e);unsigned char* p=out;for(unsigned count=0;n&&count<f->clusters;count++){if(!uf_valid(f,c))return -1;size_t k=n>4096?4096:n;if(UPDATE_IO(f->fd,p,k,uf_at(f,c),0))return -1;p+=k;n-=k;c=uf_next(f,c);}return n?-1:0;}
static unsigned char* uf_conf_entry(UpdateFat* f){return uf_find(f->rootdir,"LIMINE~1CON");}
static int uf_config(UpdateFat* f,char out[4097]){unsigned char* e=uf_conf_entry(f);if(!e||!uget32(e+28)||uget32(e+28)>4096||!uf_valid(f,uf_cluster(e))||uf_next(f,uf_cluster(e))<0x0ffffff8)return -1;memset(out,0,4097);return UPDATE_IO(f->fd,out,uget32(e+28),uf_at(f,uf_cluster(e)),0);}
static int uf_guid(const char* config,const char* key,char out[37]){const char* p=strstr(config,key);if(!p)return -1;p+=strlen(key);if(strlen(p)<36)return -1;for(unsigned i=0;i<36;i++){unsigned char c=p[i];if(i==8||i==13||i==18||i==23){if(c!='-')return -1;}else if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return -1;out[i]=c;}out[36]=0;return p[36]&&p[36]!='\n'&&p[36]!=' '?-1:0;}
static int uf_slot(UpdateFat* f,unsigned slot,uint32_t* out){unsigned char* e=uf_find(f->rootdir,slot?"KUPDATEB   ":"KUPDATEA   ");if(!e||e[11]!=16)return -1;*out=uf_cluster(e);return uf_valid(f,*out)&&uf_next(f,*out)>=0x0ffffff8?0:-1;}
static int uf_prepare(UpdateFat* f){
 char conf[4097],root[37],home[37];if(uf_config(f,conf)||uf_guid(conf,"root=PARTUUID=",root)||uf_guid(conf,"home=PARTUUID=",home)||!strcmp(root,home))return -1;
 unsigned char dirs[2][4096];uint32_t dc[2];int changed=0,created[2]={0};
 for(unsigned slot=0;slot<2;slot++){
  if(!uf_slot(f,slot,&dc[slot])){if(UPDATE_IO(f->fd,dirs[slot],4096,uf_at(f,dc[slot]),0))return -1;unsigned char* k=uf_find(dirs[slot],"KERNEL  ELF"),*r=uf_find(dirs[slot],"ROOT    IMG");if(!k||!r||uget32(k+28)!=UPDATE_KERNEL||uget32(r+28)!=UPDATE_ROOT||uf_chain(f,uf_cluster(k),UPDATE_KERNEL)||uf_chain(f,uf_cluster(r),UPDATE_ROOT))return -1;unsigned char* ready=uf_find(dirs[slot],"READY   BIN");if(!ready||uget32(ready+28)!=4096||uf_chain(f,uf_cluster(ready),4096))return -1;continue;}
  // Reserve everything in memory before publishing any references.
  unsigned char* e=uf_empty(f->rootdir);if(!e)return -1;
  uint32_t d=uf_alloc(f,4096),k=uf_alloc(f,UPDATE_KERNEL),r=uf_alloc(f,UPDATE_ROOT),ready=uf_alloc(f,4096);if(!d||!k||!r||!ready)return -1;
  memset(dirs[slot],0,4096);uf_entry(dirs[slot],".          ",16,d,0);uf_entry(dirs[slot]+32,"..         ",16,0,0);uf_entry(dirs[slot]+64,"KERNEL  ELF",32,k,UPDATE_KERNEL);uf_entry(dirs[slot]+96,"ROOT    IMG",32,r,UPDATE_ROOT);uf_entry(dirs[slot]+128,"READY   BIN",32,ready,4096);unsigned char zero[4096]={0};if(UPDATE_IO(f->fd,zero,4096,uf_at(f,ready),1))return -1;
  uf_entry(e,slot?"KUPDATEB   ":"KUPDATEA   ",16,d,0);dc[slot]=d;changed=1;created[slot]=1;
 }
 if(!changed)return 0;
 // Newly allocated data is unreachable until the last directory publication.
 for(unsigned i=0;i<2;i++)if(created[i]&&UPDATE_IO(f->fd,dirs[i],4096,uf_at(f,dc[i]),1))return -1;
 if(fsync(f->fd))return -1;
 size_t n=(size_t)f->fatsz*f->block;
 if(UPDATE_IO(f->fd,f->fat,n,32ULL*f->block,1)||fsync(f->fd)||UPDATE_IO(f->fd,f->fat,n,(32ULL+f->fatsz)*f->block,1)||fsync(f->fd))return -1;
 // FAT free counts are advisory; marking them unknown is safer than stale.
 unsigned char info[4096];for(unsigned sector=1;sector<=7;sector+=6){if(UPDATE_IO(f->fd,info,f->block,(uint64_t)sector*f->block,0))return -1;uset32(info+488,UINT32_MAX);uset32(info+492,UINT32_MAX);if(UPDATE_IO(f->fd,info,f->block,(uint64_t)sector*f->block,1))return -1;}
 if(fsync(f->fd)||UPDATE_IO(f->fd,f->rootdir,4096,uf_at(f,2),1)||fsync(f->fd))return -1;
 return 0;
}
static int uf_ready(UpdateFat* f,unsigned slot,unsigned char out[512]) {
 uint32_t d;if(uf_slot(f,slot,&d)||uf_read_named(f,d,"READY   BIN",out,512))return -1;
 return memcmp(out,"KXUPD001",8)||!uget64(out+8)||uget64(out+8)>UPDATE_KERNEL||uget64(out+16)!=UPDATE_ROOT?-1:0;
}
static int uf_set_ready(UpdateFat* f,unsigned slot,const unsigned char* header) {
 uint32_t d;unsigned char dir[4096],data[4096]={0};if(uf_slot(f,slot,&d)||UPDATE_IO(f->fd,dir,4096,uf_at(f,d),0))return -1;
 unsigned char* e=uf_find(dir,"READY   BIN");if(!e||uf_chain(f,uf_cluster(e),4096))return -1;
 if(header)memcpy(data,header,512);
 return UPDATE_IO(f->fd,data,4096,uf_at(f,uf_cluster(e)),1)||fsync(f->fd)?-1:0;
}
static int uf_active(const char* c){const char* a=strstr(c,"kernel_path: boot():/KUPDATEA/"),*b=strstr(c,"kernel_path: boot():/KUPDATEB/");return a&&(!b||a<b)?0:b?1:-1;}
static int uf_activate(UpdateFat* f,unsigned slot){
 char old[4097],root[37],home[37],conf[4096]={0};if(uf_config(f,old)||uf_guid(old,"root=PARTUUID=",root)||uf_guid(old,"home=PARTUUID=",home))return -1;
 size_t at=snprintf(conf,sizeof(conf),"timeout: 5\n# Kunix EFI generations v1\n\n");
 for(unsigned i=0;i<2;i++){
  unsigned which=i?1-slot:slot;unsigned char ready[512];if(i&&uf_ready(f,which,ready))continue;
  at+=snprintf(conf+at,sizeof(conf)-at,"/Kunix rolling (%s, slot %c)\n    protocol: limine\n    kernel_path: boot():/KUPDATE%c/KERNEL.ELF\n    module_path: boot():/KUPDATE%c/root.img\n    kernel_cmdline: home=PARTUUID=%s installed=PARTUUID=%s generation=%c\n\n",i?"previous":"selected",'A'+which,'A'+which,'A'+which,home,root,'A'+which);
 }
 at+=snprintf(conf+at,sizeof(conf)-at,"/Kunix original installation (data retained)\n    protocol: limine\n    kernel_path: boot():/KUNIX.ELF\n    kernel_cmdline: root=PARTUUID=%s home=PARTUUID=%s\n",root,home);
 if(at>=sizeof(conf))return -1;
 unsigned char* entry=uf_conf_entry(f);if(!entry)return -1;
 // Config fits one 4KiB cluster. Payload and FAT were flushed before this.
 // FAT32 is not journaled: a torn selection write may require USB recovery.
 if(UPDATE_IO(f->fd,conf,sizeof(conf),uf_at(f,uf_cluster(entry)),1)||fsync(f->fd))return -1;
 if(uget32(entry+28)!=sizeof(conf)){uset32(entry+28,sizeof(conf));if(UPDATE_IO(f->fd,f->rootdir,4096,uf_at(f,2),1)||fsync(f->fd))return -1;}
 return 0;
}
static int uf_copy(UpdateFat* f,uint32_t dir,const char name[11],int source,uint64_t from,uint32_t n,const unsigned char expected[32],int writing){
 unsigned char d[4096];if(UPDATE_IO(f->fd,d,4096,uf_at(f,dir),0))return -1;unsigned char* e=uf_find(d,name);if(!e||n>uget32(e+28)||uf_chain(f,uf_cluster(e),uget32(e+28)))return -1;
 uint32_t c=uf_cluster(e);unsigned char data[4096];UpdateHash hash;uh_init(&hash);
 for(uint32_t at=0;at<n;){uint32_t size=n-at>4096?4096:n-at;if(writing){if(UPDATE_IO(source,data,size,from+at,0)||UPDATE_IO(f->fd,data,size,uf_at(f,c),1))return -1;}else if(UPDATE_IO(f->fd,data,size,uf_at(f,c),0))return -1;uh_add(&hash,data,size);at+=size;c=uf_next(f,c);}
 unsigned char got[32];uh_done(&hash,got);return memcmp(got,expected,32)?-1:0;
}
static int uh_file(int fd,uint64_t at,uint32_t n,const unsigned char expected[32]){unsigned char data[16384],hash[32];UpdateHash s;uh_init(&s);for(uint32_t p=0;p<n;){size_t k=n-p>sizeof(data)?sizeof(data):n-p;if(UPDATE_IO(fd,data,k,at+p,0))return -1;uh_add(&s,data,k);p+=k;}uh_done(&s,hash);return memcmp(hash,expected,32)?-1:0;}
static int up_root_valid(int fd,uint64_t base) {
 static const uint32_t offsets[]={65536,8192,0,262144};unsigned char sb[1376];
 for(unsigned i=0;i<4;i++){
  if(UPDATE_IO(fd,sb,sizeof(sb),base+offsets[i],0))return -1;
  if(uget32(sb+1372)==0x19540119)return sb[209]&&!(uget32(sb+1312)&4)?0:-1;
 }
 return -1;
}
static int up_bundle(int fd,unsigned char h[512]){
 struct stat st;unsigned char elf[20];if(fstat(fd,&st)||!S_ISREG(st.st_mode)||UPDATE_IO(fd,h,512,0,0)||memcmp(h,"KXUPD001",8))return -1;
 uint64_t k=uget64(h+8),r=uget64(h+16);if(!k||k>UPDATE_KERNEL||r!=UPDATE_ROOT||(uint64_t)st.st_size!=512+k+r)return -1;
 for(unsigned i=88;i<512;i++)if(h[i])return -1;
 if(UPDATE_IO(fd,elf,sizeof(elf),512,0)||memcmp(elf,"\177ELF",4)||elf[4]!=2||elf[5]!=1||uget16(elf+18)!=62)return -1;
 if(up_root_valid(fd,512+k))return -1;
 return uh_file(fd,512,k,h+24)||uh_file(fd,512+k,r,h+56)?-1:0;
}
static int uf_verify_slot(UpdateFat* f,unsigned slot) {
 unsigned char h[512];uint32_t d;
 if(uf_ready(f,slot,h)||uf_slot(f,slot,&d))return -1;
 return uf_copy(f,d,"KERNEL  ELF",-1,0,uget64(h+8),h+24,0)||uf_copy(f,d,"ROOT    IMG",-1,0,UPDATE_ROOT,h+56,0)?-1:0;
}
// First enrollment streams from the pristine boot module. /tmp is bounded
// to 32MiB and a live USB may have no mounted home, so no bundle is materialized.
static int up_digest(int fd,uint32_t length,unsigned char out[32]) {
 unsigned char data[16384];UpdateHash hash;uh_init(&hash);
 for(uint32_t at=0;at<length;){size_t n=length-at>sizeof(data)?sizeof(data):length-at;if(UPDATE_IO(fd,data,n,at,0))return -1;uh_add(&hash,data,n);at+=n;}
 uh_done(&hash,out);return 0;
}
static int up_live_header(int kernel,int source,unsigned char h[512]) {
 struct stat k;unsigned char elf[20];
 if(fstat(kernel,&k)||!S_ISREG(k.st_mode)||k.st_size<=0||k.st_size>UPDATE_KERNEL||UPDATE_IO(kernel,elf,sizeof(elf),0,0)||memcmp(elf,"\177ELF",4)||elf[4]!=2||elf[5]!=1||uget16(elf+18)!=62)return -1;
 if(up_root_valid(source,0))return -1;
 memset(h,0,512);memcpy(h,"KXUPD001",8);uset32(h+8,k.st_size);uset32(h+16,UPDATE_ROOT);
 return up_digest(kernel,k.st_size,h+24)||up_digest(source,UPDATE_ROOT,h+56)?-1:0;
}
static int up_from_live(int* kernel,int* source,unsigned char h[512]) {
 *kernel=open("/install/KUNIX.ELF",O_RDONLY|O_NOFOLLOW);*source=open("/dev/install-source",O_RDONLY);
 unsigned long long size=0;
 if(*kernel<0||*source<0||ioctl(*source,KDISK_BLKGETSIZE64,&size)||size!=UPDATE_ROOT)return -1;
 return up_live_header(*kernel,*source,h);
}
static void up_guid_text(const unsigned char* p,char out[37]) {
 static const unsigned order[]={3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15};static const char hex[]="0123456789abcdef";unsigned n=0;
 for(unsigned i=0;i<16;i++){if(i==4||i==6||i==8||i==10)out[n++]='-';out[n++]=hex[p[order[i]]>>4];out[n++]=hex[p[order[i]]&15];}out[n]=0;
}
static int up_layout(const struct kdisk_info* esp,const char* config) {
 char root[37],home[37];if(esp->slot!=1||uf_guid(config,"root=PARTUUID=",root)||uf_guid(config,"home=PARTUUID=",home)||!strcmp(root,home))return -1;
 unsigned found=0;
 for(unsigned n=16;n<128;n++){char path[32],guid[37];snprintf(path,sizeof(path),"/dev/disk%u",n);int fd=open(path,O_RDONLY);if(fd<0)continue;struct kdisk_info i;int bad=ioctl(fd,KDISK_INFO,&i);close(fd);if(bad||i.parent!=esp->parent)continue;up_guid_text(i.guid,guid);if(i.slot==2&&!strcmp(guid,root))found|=1;if(i.slot==3&&!strcmp(guid,home))found|=2;}
 return found==3?0:-1;
}
static int up_running(void) {
 char line[4096]={0};int fd=open("/proc/cmdline",O_RDONLY);if(fd<0)return -2;
 ssize_t n=read(fd,line,sizeof(line)-1);close(fd);if(n<0)return -2;
 const char* p=strstr(line,"generation=");if(!p)return -1;
 if(p!=line&&p[-1]!=' ')return -2;
 p+=11;
 if((p[0]!='A'&&p[0]!='B')||(p[1]&&p[1]!=' '&&p[1]!='\n'))return -2;
 return *p-'A';
}
int main(int argc,char** argv){
 if(argc!=4||strcmp(argv[1],"--esp")){fprintf(stderr,"usage: kunix-update --esp /dev/diskN BUNDLE.pkg|--from-live|--rollback\n");return 1;}
 if(geteuid()){fprintf(stderr,"kunix-update: root required\n");return 1;}
 if(mkdir("/tmp/kunix-update.lock",0700)){fprintf(stderr,"kunix-update: another update is active, or /tmp/kunix-update.lock remains after interruption; remove it only after confirming no updater is running\n");return 1;}
 int rollback=!strcmp(argv[3],"--rollback"),bundle=-1,live_kernel=-1,live_root=-1,fd=-1,result=1;unsigned char header[512]={0};UpdateFat fat={0};
 int from_live=!strcmp(argv[3],"--from-live");
 if(!rollback){
  int bad;if(from_live)bad=up_from_live(&live_kernel,&live_root,header);else{bundle=open(argv[3],O_RDONLY|O_NOFOLLOW);bad=bundle<0||up_bundle(bundle,header);}
  if(bad){fprintf(stderr,"kunix-update: invalid bundle (format, image or SHA-256)\n");goto done;}}
 fd=open(argv[2],O_RDWR);struct kdisk_info info;
 if(fd<0||ioctl(fd,KDISK_INFO,&info)||(info.flags&(KDISK_ESP|KDISK_NVME))!=(KDISK_ESP|KDISK_NVME)||!info.slot||(info.flags&(KDISK_WHOLE|KDISK_RAM|KDISK_READONLY))||uf_open(&fat,fd,info.sectors*512,info.block_size)){fprintf(stderr,"kunix-update: requires the unmounted Bernard EFI partition, never a whole disk or UFS partition\n");goto done;}
 char config[4097];if(uf_config(&fat,config)||up_layout(&info,config)){fprintf(stderr,"kunix-update: boot configuration does not match Bernard root/home partitions\n");goto done;}int active=uf_active(config);unsigned next=active<0?0:1-active;
 int kernel_source=from_live?live_kernel:bundle,root_source=from_live?live_root:bundle;
 uint64_t kernel_offset=from_live?0:512,root_offset=from_live?0:512+uget64(header+8);
 int running=up_running();
 if(rollback){if(active<0||uf_verify_slot(&fat,next)){fprintf(stderr,"kunix-update: previous generation is absent or incomplete; original installation retained\n");goto done;}}
 else if((active>=0&&running!=active)||(active<0&&running!=-1)){fprintf(stderr,"kunix-update: boot the selected generation before another update; fallback boot requires explicit rollback first\n");goto done;}
 else {
  puts("Verified bundle. Preparing inactive EFI generation; installed UFS data is retained.");
  if(uf_prepare(&fat)){fprintf(stderr,"kunix-update: slot preparation failed; boot files retained, inspect EFI filesystem before retrying\n");goto done;}
  uint32_t dir;
  if(uf_set_ready(&fat,next,NULL)||(active>=0&&uf_activate(&fat,active)))goto done;
  if(uf_slot(&fat,next,&dir)||uf_copy(&fat,dir,"KERNEL  ELF",kernel_source,kernel_offset,uget64(header+8),header+24,1)||uf_copy(&fat,dir,"ROOT    IMG",root_source,root_offset,UPDATE_ROOT,header+56,1)||fsync(fd)||uf_copy(&fat,dir,"KERNEL  ELF",kernel_source,kernel_offset,uget64(header+8),header+24,0)||uf_copy(&fat,dir,"ROOT    IMG",bundle,0,UPDATE_ROOT,header+56,0)){fprintf(stderr,"kunix-update: staging/readback failed; selected generation unchanged\n");goto done;}
  if(uf_set_ready(&fat,next,header))goto done;
  // First enrollment has no valid previous slot yet. Initialize both so every
  // generated boot menu entry is bootable; later updates only touch inactive.
  if(active<0){if(uf_slot(&fat,1,&dir)||uf_copy(&fat,dir,"KERNEL  ELF",kernel_source,kernel_offset,uget64(header+8),header+24,1)||uf_copy(&fat,dir,"ROOT    IMG",root_source,root_offset,UPDATE_ROOT,header+56,1)||fsync(fd)||uf_copy(&fat,dir,"KERNEL  ELF",bundle,0,uget64(header+8),header+24,0)||uf_copy(&fat,dir,"ROOT    IMG",bundle,0,UPDATE_ROOT,header+56,0)||uf_set_ready(&fat,1,header))goto done;}
 }
 if(uf_activate(&fat,next)){fprintf(stderr,"kunix-update: selection failed; use the retained original boot option or recovery USB\n");goto done;}
 printf("kunix-update: selected slot %c. Reboot when ready. Previous and original boot options retained.\n",'A'+next);result=0;
done:uf_close(&fat);if(fd>=0)close(fd);if(bundle>=0)close(bundle);if(live_kernel>=0)close(live_kernel);if(live_root>=0)close(live_root);rmdir("/tmp/kunix-update.lock");return result;
}
