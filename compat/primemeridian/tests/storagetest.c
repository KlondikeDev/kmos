// This file has been generated in whole or in part by a Large Language Model
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/statvfs.h>
#include "../../../include/disk_ioctl.h"
static int passed,failed; static FILE* serial;
#define CHECK(name,expr) do { int check_result=!!(expr); passed+=check_result;failed+=!check_result;printf("%s %s\n",check_result?"PASS":"FAIL",name);if(serial){fprintf(serial,"%s %s\n",check_result?"PASS":"FAIL",name);fflush(serial);} }while(0)
static int persistent(int verify) {
 const char* names[]={"/phase3-root.txt","/home/phase3-home.txt"};
 for(unsigned i=0;i<2;i++) {
  int fd=open(names[i],verify?O_RDONLY:O_WRONLY|O_CREAT|O_TRUNC,0600);char text[32]={0};
  int ok=fd>=0;
  if(ok)ok=verify ? read(fd,text,sizeof(text))==15 && !memcmp(text,"Kunix persisted",15) : write(fd,"Kunix persisted",15)==15 && fsync(fd)==0;
  CHECK(i?"persistent home data":"persistent root data",ok);if(fd>=0)close(fd);
 }
 struct statvfs root,home;
 CHECK("root grew beyond live image",!statvfs("/",&root) && (unsigned long long)root.f_blocks*root.f_frsize>128ULL*1024*1024);
 CHECK("home grew beyond its seed",!statvfs("/home",&home) && (unsigned long long)home.f_blocks*home.f_frsize>16ULL*1024*1024);
 errno=0;int source=open("/dev/install-source",O_RDONLY);
 CHECK("installed boot has no live source",source<0 && errno==ENOENT);if(source>=0)close(source);
 errno=0;int raw=open("/dev/disk8",O_RDWR);
 CHECK("mounted NVMe parent refuses raw writes",raw<0 && errno==EBUSY);if(raw>=0)close(raw);
 return 0;
}
int main(int argc,char**argv) {
 serial=fopen("/dev/serial","w");
 if(argc>1 && (!strcmp(argv[1],"persist") || !strcmp(argv[1],"verify")))persistent(!strcmp(argv[1],"verify"));
 else {
  int fd=open("/dev/disk8",O_RDONLY);struct kdisk_info info={0};
  CHECK("NVMe raw device discovered",fd>=0 && !ioctl(fd,KDISK_INFO,&info) && (info.flags&(KDISK_WHOLE|KDISK_NVME))==(KDISK_WHOLE|KDISK_NVME));
  unsigned long long bytes=0;unsigned block=0;
  CHECK("native sector and byte size ioctls",fd>=0 && !ioctl(fd,KDISK_BLKGETSIZE64,&bytes) && !ioctl(fd,KDISK_BLKSSZGET,&block) && bytes==info.sectors*512 && block==info.block_size && (block==512||block==4096));
  pid_t child=fork();if(!child) { if(setuid(1000))_exit(2);int d=open("/dev/disk8",O_RDONLY);_exit(d<0 && errno==EACCES?0:1); }
  int status=0;waitpid(child,&status,0);CHECK("unprivileged raw disk access denied",WIFEXITED(status)&&!WEXITSTATUS(status));
  if(fd>=0)close(fd);
  if(argc>1 && !strcmp(argv[1],"write")) {
   printf("Disposable VM disk test: type YES to write and restore a test range.\n");fflush(stdout);
   if(serial){fprintf(serial,"storage: confirmation required\n");fflush(serial);}
   char answer[16];if(!fgets(answer,sizeof(answer),stdin)||strcmp(answer,"YES\n"))return 2;
   fd=open("/dev/disk8",O_RDWR);unsigned char old[16384],pattern[16384],actual[16384];
   for(unsigned i=0;i<sizeof(pattern);i++)pattern[i]=(unsigned char)(i*17+29);
   int saved=fd>=0 && pread(fd,old,sizeof(old),2*1024*1024)==sizeof(old);
   CHECK("NVMe multi-page write and flush",saved && pwrite(fd,pattern,sizeof(pattern),2*1024*1024)==sizeof(pattern) && !fsync(fd));
   CHECK("NVMe data reads back",saved && pread(fd,actual,sizeof(actual),2*1024*1024)==sizeof(actual) && !memcmp(actual,pattern,sizeof(actual)));
   CHECK("unaligned native sector RMW",saved && pwrite(fd,"XYZ",3,2*1024*1024+511)==3 && !fsync(fd) && pread(fd,actual,sizeof(actual),2*1024*1024)==sizeof(actual) && actual[510]==pattern[510] && !memcmp(actual+511,"XYZ",3) && actual[514]==pattern[514]);
   CHECK("restore test range",saved && pwrite(fd,old,sizeof(old),2*1024*1024)==sizeof(old) && !fsync(fd));if(fd>=0)close(fd);
  }
 }
 printf("storagetest: %d passed, %d failed\n",passed,failed);
 if(serial){fprintf(serial,"storagetest: %d passed, %d failed\n",passed,failed);fclose(serial);}return !!failed;
}
