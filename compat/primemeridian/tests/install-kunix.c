// This file has been generated in whole or in part by a Large Language Model
// Bernard installer: all destructive operations follow a complete plan and YES.
#define _GNU_SOURCE
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/random.h>
#include <sys/wait.h>
#include <termios.h>
#include "../../../include/disk_ioctl.h"
#define MiB (1024ULL*1024)
#define GiB (1024ULL*MiB)
typedef struct { uint64_t start,size; unsigned char guid[16]; } Part;
typedef struct { uint64_t bytes; unsigned block; Part part[3]; unsigned char guid[16]; } Plan;
static int serial=-1;
static void report(const char* fmt,...) {
 char line[512]; va_list ap; va_start(ap,fmt); vsnprintf(line,sizeof(line),fmt,ap); va_end(ap);
 printf("%s\n",line); fflush(stdout);
 if(serial>=0) { (void)write(serial,line,strlen(line)); (void)write(serial,"\n",1); }
}
static void put16(unsigned char* p,unsigned n) { p[0]=n; p[1]=n>>8; }
static void put32(unsigned char* p,uint32_t n) { for(unsigned i=0;i<4;i++) p[i]=n>>(8*i); }
static void put64(unsigned char* p,uint64_t n) { for(unsigned i=0;i<8;i++) p[i]=n>>(8*i); }
static uint32_t crc32(const unsigned char* p,size_t n) {
 uint32_t c=~0U; for(size_t i=0;i<n;i++) { c^=p[i]; for(unsigned j=0;j<8;j++) c=(c>>1)^((0-(c&1))&0xedb88320U); } return ~c;
}
static int transfer(int fd,void* buffer,size_t n,uint64_t at,int writing) {
 if(at>INT64_MAX || n>(uint64_t)INT64_MAX-at) { errno=EOVERFLOW; return -1; }
 unsigned char* p=buffer;
 while(n) {
  ssize_t got=writing ? pwrite(fd,p,n,(off_t)at) : pread(fd,p,n,(off_t)at);
  if(got<0 && errno==EINTR) continue;
  if(got<=0) { if(!got) errno=EIO; return -1; }
  p+=got; n-=got; at+=got;
 }
 return 0;
}
static int copy_file(int from,int to,uint64_t size,uint64_t offset) {
 unsigned char* buffer=malloc(MiB); if(!buffer) return -1;
 int result=0;
 for(uint64_t at=0;at<size;) {
  size_t n=size-at>MiB ? MiB : size-at;
  if(transfer(from,buffer,n,at,0) || transfer(to,buffer,n,offset+at,1)) { result=-1; break; }
  at+=n;
 }
 free(buffer); return result;
}
static int make_plan(const struct kdisk_info* info,Plan* p) {
 memset(p,0,sizeof(*p));
 if((info->block_size!=512 && info->block_size!=4096) || info->sectors>INT64_MAX/512 || info->sectors*512<2*GiB || info->sectors%(info->block_size/512)) return -1;
 p->bytes=info->sectors*512; p->block=info->block_size;
 uint64_t end=(p->bytes-p->block-16384)/MiB*MiB;
 p->part[0].start=MiB; p->part[0].size=512*MiB;
 p->part[1].start=513*MiB;
 uint64_t remain=end-p->part[1].start;
 p->part[1].size=(remain/2)/MiB*MiB;
 if(p->part[1].size>32*GiB) p->part[1].size=32*GiB;
 p->part[2].start=p->part[1].start+p->part[1].size;
 p->part[2].size=end-p->part[2].start;
 return p->part[1].size>=128*MiB && p->part[2].size>=16*MiB ? 0 : -1;
}
static int random_guid(unsigned char guid[16]) {
 size_t at=0;
 while(at<16) { ssize_t n=getrandom(guid+at,16-at,0); if(n<0 && errno==EINTR) continue; if(n<=0) return -1; at+=n; }
 guid[7]=(guid[7]&15)|0x40; guid[8]=(guid[8]&63)|0x80; return 0;
}
static void guid_text(const unsigned char* guid,char out[37]) {
 static const unsigned char order[]={3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15};
 static const char hex[]="0123456789abcdef"; unsigned at=0;
 for(unsigned i=0;i<16;i++) { if(i==4 || i==6 || i==8 || i==10) out[at++]='-'; unsigned b=guid[order[i]]; out[at++]=hex[b>>4]; out[at++]=hex[b&15]; } out[at]=0;
}
static int plan_guids(Plan* p) {
 for(unsigned i=0;i<4;i++) {
  unsigned char* guid=i ? p->part[i-1].guid : p->guid;
  if(random_guid(guid)) return -1;
  if(i && !memcmp(guid,p->guid,16)) return -1;
  for(unsigned j=1;j<i;j++) if(!memcmp(guid,p->part[j-1].guid,16)) return -1;
 }
 return 0;
}
static int write_gpt(int fd,const Plan* p) {
 static const unsigned char esp[]={0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,0xba,0x4b,0,0xa0,0xc9,0x3e,0xc9,0x3b};
 static const unsigned char ufs[]={0xb6,0x7c,0x6e,0x51,0xcf,0x6e,0xd6,0x11,0x8f,0xf8,0,2,0x2d,9,0x71,0x2b};
 unsigned char entries[16384]={0},header[4096]={0},mbr[4096]={0};
 uint64_t blocks=p->bytes/p->block, arrayblocks=sizeof(entries)/p->block;
 const char* names[]={"EFI system","Kunix Bernard","Kunix home"};
 for(unsigned i=0;i<3;i++) {
  unsigned char* e=entries+i*128; memcpy(e,i ? ufs : esp,16); memcpy(e+16,p->part[i].guid,16);
  put64(e+32,p->part[i].start/p->block); put64(e+40,(p->part[i].start+p->part[i].size)/p->block-1);
  for(unsigned j=0;names[i][j];j++) put16(e+56+j*2,names[i][j]);
 }
 mbr[510]=0x55; mbr[511]=0xaa; mbr[447]=0; mbr[448]=2; mbr[450]=0xee; memset(mbr+451,0xff,3); put32(mbr+454,1); put32(mbr+458,blocks-1>UINT32_MAX ? UINT32_MAX : blocks-1);
 if(transfer(fd,mbr,p->block,0,1)) return -1;
 for(unsigned backup=0;backup<2;backup++) {
  memset(header,0,sizeof(header)); memcpy(header,"EFI PART",8); put32(header+8,0x10000); put32(header+12,92);
  put64(header+24,backup ? blocks-1 : 1); put64(header+32,backup ? 1 : blocks-1);
  put64(header+40,2+arrayblocks); put64(header+48,blocks-arrayblocks-2); memcpy(header+56,p->guid,16);
  uint64_t array=backup ? blocks-arrayblocks-1 : 2;
  put64(header+72,array); put32(header+80,128); put32(header+84,128); put32(header+88,crc32(entries,sizeof(entries))); put32(header+16,crc32(header,92));
  if(transfer(fd,entries,sizeof(entries),array*p->block,1) || transfer(fd,header,p->block,(backup ? blocks-1 : 1)*p->block,1)) return -1;
 }
 return fsync(fd);
}
// FAT32 clusters are always 4096 bytes, including on native 4 KiB namespaces.
static void dir_entry(unsigned char* e,const char name[11],unsigned attr,uint32_t cluster,uint32_t size) {
 memset(e,0,32); memcpy(e,name,11); e[11]=attr; put16(e+20,cluster>>16); put16(e+26,cluster); put32(e+28,size);
 // FAT's earliest representable date, 1980-01-01.
 put16(e+16,0x21); put16(e+18,0x21); put16(e+24,0x21);
}
static void lfn_entry(unsigned char* e,const char* name,const unsigned char alias[11]) {
 static const unsigned offsets[]={1,3,5,7,9,14,16,18,20,22,24,28,30};
 memset(e,0xff,32); e[0]=0x41; e[11]=0x0f; e[12]=0; e[26]=e[27]=0;
 unsigned char sum=0; for(unsigned i=0;i<11;i++) sum=((sum&1)<<7)+(sum>>1)+alias[i]; e[13]=sum;
 size_t len=strlen(name); for(unsigned i=0;i<13;i++) put16(e+offsets[i],i<len ? (unsigned char)name[i] : i==len ? 0 : 0xffff);
}
static int fat_write(int fd,const Plan* p,int kernel,int loader,const char* config) {
 struct stat ks,ls; if(fstat(kernel,&ks) || fstat(loader,&ls) || ks.st_size<=0 || ls.st_size<=0 || (uint64_t)ks.st_size>UINT32_MAX || (uint64_t)ls.st_size>UINT32_MAX || strlen(config)>UINT32_MAX) return -1;
 unsigned b=p->block,spc=4096/b;
 uint32_t total=p->part[0].size/b, fatsz=1;
 // Choose the smallest FAT large enough for its resulting data area.
 while((uint64_t)fatsz*b/4 < (total-32-2*fatsz)/spc+2) fatsz++;
 uint32_t clusters=(total-32-2*fatsz)/spc;
 if(clusters<65525 || clusters>=0x0ffffff5) { errno=EINVAL; return -1; }
 uint32_t nc[3]={((uint64_t)ks.st_size+4095)/4096,((uint64_t)ls.st_size+4095)/4096,(strlen(config)+4095)/4096};
 uint64_t used=3ULL+nc[0]+nc[1]+nc[2]; if(used>clusters) { errno=ENOSPC; return -1; }
 uint32_t starts[3]={5,5+nc[0],5+nc[0]+nc[1]};
 unsigned char* fat=calloc(fatsz,b); if(!fat) return -1;
 put32(fat,0x0ffffff8); put32(fat+4,0x0fffffff);
 for(unsigned i=2;i<5;i++) put32(fat+4*i,0x0fffffff);
 for(unsigned f=0;f<3;f++) for(uint32_t c=0;c<nc[f];c++) put32(fat+4*(starts[f]+c),c+1==nc[f] ? 0x0fffffff : starts[f]+c+1);
 unsigned char reserved[32*4096]={0},dir[4096]={0};
 unsigned char* boot=reserved;
 boot[0]=0xeb; boot[1]=0x58; boot[2]=0x90; memcpy(boot+3,"KUNIX   ",8); put16(boot+11,b); boot[13]=spc; put16(boot+14,32); boot[16]=2; boot[21]=0xf8;
 put16(boot+24,63); put16(boot+26,255); put32(boot+28,p->part[0].start/b); put32(boot+32,total); put32(boot+36,fatsz); put32(boot+44,2); put16(boot+48,1); put16(boot+50,6); boot[64]=0x80; boot[66]=0x29; put32(boot+67,crc32(p->guid,16)); memcpy(boot+71,"KUNIX ESP  ",11); memcpy(boot+82,"FAT32   ",8); boot[510]=0x55; boot[511]=0xaa;
 unsigned char* fsinfo=reserved+b; put32(fsinfo,0x41615252); put32(fsinfo+484,0x61417272); put32(fsinfo+488,clusters-used); put32(fsinfo+492,used<clusters ? used+2 : UINT32_MAX); put32(fsinfo+508,0xaa550000);
 memcpy(reserved+6*b,boot,b); memcpy(reserved+7*b,fsinfo,b);
 uint64_t base=p->part[0].start, data=base+(32ULL+2ULL*fatsz)*b;
 int result=-1;
 if(transfer(fd,reserved,32*b,base,1) || transfer(fd,fat,(size_t)fatsz*b,base+32*b,1) || transfer(fd,fat,(size_t)fatsz*b,base+(32ULL+fatsz)*b,1)) goto done;
 dir_entry(dir,"KUNIX ESP  ",8,0,0); dir_entry(dir+32,"EFI        ",16,3,0); dir_entry(dir+64,"KUNIX   ELF",32,starts[0],ks.st_size);
 lfn_entry(dir+96,"limine.conf",(const unsigned char*)"LIMINE~1CON"); dir_entry(dir+128,"LIMINE~1CON",32,starts[2],strlen(config));
 if(transfer(fd,dir,4096,data,1)) goto done;
 memset(dir,0,sizeof(dir)); dir_entry(dir,".          ",16,3,0); dir_entry(dir+32,"..         ",16,0,0); dir_entry(dir+64,"BOOT       ",16,4,0);
 if(transfer(fd,dir,4096,data+4096,1)) goto done;
 memset(dir,0,sizeof(dir)); dir_entry(dir,".          ",16,4,0); dir_entry(dir+32,"..         ",16,3,0); dir_entry(dir+64,"BOOTX64 EFI",32,starts[1],ls.st_size);
 if(transfer(fd,dir,4096,data+8192,1)) goto done;
 for(unsigned f=0;f<3;f++) {
  uint64_t size=f==0 ? ks.st_size : f==1 ? ls.st_size : (off_t)strlen(config);
  uint64_t offset=data+(uint64_t)(starts[f]-2)*4096;
  if(f<2) { if(copy_file(f ? loader : kernel,fd,size,offset)) goto done; }
  else if(transfer(fd,(void*)config,size,offset,1)) goto done;
  // Initialize the final partial cluster rather than exposing old target contents.
  if(size%4096) { memset(dir,0,4096); if(transfer(fd,dir,4096-size%4096,offset+size,1)) goto done; }
 }
 result=fsync(fd);
done:
 free(fat); return result;
}
static int allowed(const struct kdisk_info* i) {
 return (i->flags&(KDISK_WHOLE|KDISK_NVME))==(KDISK_WHOLE|KDISK_NVME) && !(i->flags&(KDISK_MOUNTED|KDISK_RAM|KDISK_READONLY));
}
static int regular_source(const char* path,uint64_t exact) {
 int fd=open(path,O_RDONLY); struct stat st;
 if(fd<0) return -1;
 if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<=0 || (exact && (uint64_t)st.st_size!=exact)) { close(fd); errno=EINVAL; return -1; }
 return fd;
}
static int grow_part(const char* path,uint64_t size) {
 char bytes[32]; snprintf(bytes,sizeof(bytes),"%" PRIu64,size);
 pid_t pid=fork(); if(pid<0) return -1;
 if(!pid) { execl("/bin/ufsgrow","ufsgrow",path,bytes,(char*)NULL); _exit(127); }
 int status; pid_t waited; do { waited=waitpid(pid,&status,0); } while(waited<0 && errno==EINTR);
 return waited==pid && WIFEXITED(status) && WEXITSTATUS(status)==0 ? 0 : -1;
}
static int partition_paths(unsigned parent,char paths[3][32],const Plan* p) {
 unsigned found=0;
 for(unsigned d=16;d<128;d++) {
  char path[32]; snprintf(path,sizeof(path),"/dev/disk%u",d); int fd=open(path,O_RDONLY); if(fd<0) continue;
  struct kdisk_info i; int ok=ioctl(fd,KDISK_INFO,&i); close(fd);
  if(!ok && i.parent==parent && i.slot>=1 && i.slot<=3 && !(i.flags&(KDISK_WHOLE|KDISK_MOUNTED|KDISK_READONLY)) && !memcmp(i.guid,p->part[i.slot-1].guid,16) && i.sectors==p->part[i.slot-1].size/512) {
   if(found&(1u<<(i.slot-1))) return -1;
   strcpy(paths[i.slot-1],path); found|=1u<<(i.slot-1);
  }
 }
 return found==7 ? 0 : -1;
}
// Canonical terminal mode otherwise holds Escape/F3 until Enter is pressed.
// Pipes retain normal line reading so automation must still supply literal YES.
static int read_line(char* line,size_t cap) {
 if(!isatty(STDIN_FILENO)) {
  if(!fgets(line,cap,stdin)) return 0;
  return !strchr(line,27) && !strstr(line,"F3");
 }
 struct termios saved,raw;
 if(tcgetattr(STDIN_FILENO,&saved)) return 0;
 raw=saved; raw.c_lflag&=~(ICANON|ECHO); raw.c_cc[VMIN]=0; raw.c_cc[VTIME]=1;
 if(tcsetattr(STDIN_FILENO,TCSANOW,&raw)) return 0;
 size_t at=0; int result=0;
 for(;;) {
  unsigned char ch; ssize_t n=read(STDIN_FILENO,&ch,1);
  if(n<0 && errno==EINTR) continue;
  if(n<0) break;
  if(!n) continue;
  if(ch==27) {
   char sequence[8]={0}; unsigned i=0;
   while(i<sizeof(sequence)-1) {
    n=read(STDIN_FILENO,sequence+i,1);
    if(n<0 && errno==EINTR) continue;
    if(n!=1) break;
    if(sequence[i++]=='~' || (i==2 && sequence[0]=='O')) break;
   }
   if(!strcmp(sequence,"[19~")) ch='\n'; // F8 submits the current line.
   else break; // Escape and F3 always cancel.
  }
  if(ch=='\n' || ch=='\r') { line[at]='\n'; line[at+1]=0; putchar('\n'); fflush(stdout); result=1; break; }
  if(ch==127 || ch==8) { if(at) { at--; fputs("\b \b",stdout); fflush(stdout); } continue; }
  if(ch<32 || ch>126) continue;
  if(at+2>=cap) break;
  line[at++]=ch; putchar(ch); fflush(stdout);
 }
 (void)tcsetattr(STDIN_FILENO,TCSANOW,&saved);
 return result;
}
int main(int argc,char** argv) {
 serial=open("/dev/serial",O_WRONLY);
 int plan_only=0,fd=-1,source=-1,kernel=-1,loader=-1,home=-1,result=1; const char* device=NULL; char chosen[32];
 for(int i=1;i<argc;i++) { if(!strcmp(argv[i],"--plan")) plan_only=1; else if(!device && argv[i][0]!='/') { fprintf(stderr,"Usage: install-kunix [/dev/diskN] [--plan]\n"); return 1; } else if(!device) device=argv[i]; else { fprintf(stderr,"Unexpected argument\n"); return 1; } }
 if(geteuid()!=0) { report("Installation requires root."); return 1; }
 if(isatty(STDOUT_FILENO)) printf("\033[44;37m\033[2J\033[HBernard Setup\n\n");
 if(isatty(STDIN_FILENO) && !plan_only) {
  report("Bernard Setup installs Kunix and a persistent home on an NVMe disk.");
  report("ENTER/F8 = continue    F3/Escape = cancel");
 }
 if(!device) {
  report("Available unmounted NVMe disks:"); unsigned candidates=0;
  for(unsigned d=8;d<16;d++) {
   char path[32]; snprintf(path,sizeof(path),"/dev/disk%u",d); int probe=open(path,O_RDONLY); if(probe<0) continue;
   struct kdisk_info i; if(!ioctl(probe,KDISK_INFO,&i) && allowed(&i)) { report("  %s  %llu MiB",path,i.sectors/2048); candidates++; }
   close(probe);
  }
  if(!candidates) { report("No eligible target disks found."); goto done; }
  report("Enter the target disk number (8-15), or F3/Escape to cancel:");
  char line[80]; if(!read_line(line,sizeof(line))) { report("Installation canceled; no writes performed."); report("install: canceled"); result=0; goto done; }
  char* end; errno=0; unsigned long n=strtoul(line,&end,10);
  if(errno || end==line || (*end!='\n' && *end) || n<8 || n>15) { report("Invalid disk selection."); goto done; }
  snprintf(chosen,sizeof(chosen),"/dev/disk%lu",n); device=chosen;
 }
 fd=open(device,O_RDONLY); struct kdisk_info info; Plan p;
 if(fd<0 || ioctl(fd,KDISK_INFO,&info) || !allowed(&info) || make_plan(&info,&p)) { report("Target must be an unmounted, writable whole NVMe disk of at least 2 GiB."); goto done; }
 if(plan_guids(&p)) { report("Cannot obtain unique partition identifiers."); goto done; }
 char rootguid[37],homeguid[37]; guid_text(p.part[1].guid,rootguid); guid_text(p.part[2].guid,homeguid);
 report("Target: %s (%" PRIu64 " MiB), native block %u bytes",device,p.bytes/MiB,p.block);
 report("The existing partition table and files on this disk will be destroyed.");
 for(unsigned i=0;i<3;i++) report("  %s: start %" PRIu64 " MiB, size %" PRIu64 " MiB",i==0 ? "EFI FAT32" : i==1 ? "Bernard UFS2 root" : "Persistent UFS2 home",p.part[i].start/MiB,p.part[i].size/MiB);
 report("Root PARTUUID=%s",rootguid); report("Home PARTUUID=%s",homeguid);
 // Keep every source open before confirmation so later path changes cannot swap it.
 source=open("/dev/install-source",O_RDONLY); uint64_t source_size=0;
 if(source<0 || ioctl(source,KDISK_BLKGETSIZE64,&source_size) || source_size!=128*MiB) { report("Missing or invalid pristine 128 MiB install source."); goto done; }
 kernel=regular_source("/install/KUNIX.ELF",0); loader=regular_source("/install/BOOTX64.EFI",0); home=regular_source("/install/home.img",16*MiB);
 struct stat growstat;
 if(kernel<0 || loader<0 || home<0 || stat("/bin/ufsgrow",&growstat) || !S_ISREG(growstat.st_mode) || access("/bin/ufsgrow",X_OK)) { report("Installation files or /bin/ufsgrow are missing or invalid."); goto done; }
 struct stat ks,ls; if(fstat(kernel,&ks) || fstat(loader,&ls) || (uint64_t)ks.st_size+(uint64_t)ls.st_size>500*MiB) { report("Boot files will not fit in the EFI partition."); goto done; }
 if(plan_only) { report("Plan only: preflight passed; no writes performed."); report("install: 1 passed, 0 failed"); result=0; goto done; }
 report("ENTER/F8 = continue by typing YES    F3/Escape = cancel");
 report("Type exactly YES to erase %s and install Bernard:",device);
 report("install: confirmation required");
 char answer[80]; if(!read_line(answer,sizeof(answer))) { report("Installation canceled; no writes performed."); report("install: canceled"); result=0; goto done; }
 if(strcmp(answer,"YES\n") && strcmp(answer,"YES")) { report("Installation canceled; no writes performed."); report("install: canceled"); result=0; goto done; }
 // Recheck mount and device identity after confirmation, before acquiring write access.
 struct kdisk_info again;
 if(ioctl(fd,KDISK_INFO,&again) || memcmp(&again,&info,sizeof(info)) || !allowed(&again)) { report("Target changed while confirmation was pending."); goto done; }
 close(fd); fd=open(device,O_RDWR);
 if(fd<0 || ioctl(fd,KDISK_INFO,&again) || memcmp(&again,&info,sizeof(info)) || !allowed(&again)) { report("Target is no longer writable and unmounted."); goto done; }
 report("Writing GPT partition table...");
 if(write_gpt(fd,&p)) goto io_error;
 report("Copying pristine Bernard root..."); if(copy_file(source,fd,source_size,p.part[1].start)) goto io_error;
 report("Copying empty home filesystem..."); if(copy_file(home,fd,16*MiB,p.part[2].start) || fsync(fd)) goto io_error;
 if(ioctl(fd,KDISK_RESCAN,0)<0) { report("Partition rescan failed."); goto done; }
 char paths[3][32];
 if(partition_paths(info.parent,paths,&p)) { report("Cannot find and verify the new partition devices."); goto done; }
 report("Growing the root filesystem..."); if(grow_part(paths[1],p.part[1].size)) { report("Root filesystem growth failed."); goto done; }
 report("Growing the persistent home filesystem..."); if(grow_part(paths[2],p.part[2].size)) { report("Home filesystem growth failed."); goto done; }
 char config[512]; snprintf(config,sizeof(config),"timeout: 3\n\n/Kunix Millennium Operating System (Bernard)\n    protocol: limine\n    kernel_path: boot():/KUNIX.ELF\n    kernel_cmdline: root=PARTUUID=%s home=PARTUUID=%s\n",rootguid,homeguid);
 report("Formatting EFI FAT32 and installing Limine..."); if(fat_write(fd,&p,kernel,loader,config) || fsync(fd)) goto io_error;
 report("install: 1 passed, 0 failed"); report("INSTALL-KUNIX: SUCCESS"); report("Remove the live USB, reboot, and verify that /home persists across reboots."); result=0; goto done;
io_error:
 report("Installation stopped after an I/O error: %s. The target is incomplete.",strerror(errno));
done:
 if(result) report("install: 0 passed, 1 failed");
 if(fd>=0) close(fd);
 if(source>=0) close(source);
 if(kernel>=0) close(kernel);
 if(loader>=0) close(loader);
 if(home>=0) close(home);
 if(serial>=0) close(serial);
 if(isatty(STDOUT_FILENO)) printf("\033[0m");
 return result;
}
