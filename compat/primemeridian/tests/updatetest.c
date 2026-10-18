// This file has been generated in whole or in part by a Large Language Model
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "../../../include/disk_ioctl.h"
static int passed,failed;static FILE* serial;
#define CHECK(name,expr) do{int check_result=!!(expr);passed+=check_result;failed+=!check_result;printf("%s %s\n",check_result?"PASS":"FAIL",name);if(serial){fprintf(serial,"%s %s\n",check_result?"PASS":"FAIL",name);fflush(serial);}}while(0)
static int generation(char wanted){char line[4096]={0};int fd=open("/proc/cmdline",O_RDONLY);if(fd<0)return 0;ssize_t n=read(fd,line,sizeof line-1);close(fd);if(n<0)return 0;char* p=strstr(line,"generation=");return wanted?p&&p[11]==wanted:!p;}
static void sentinel(const char* path,const char* label){char text[32]={0};int fd=open(path,O_RDONLY);int ok=fd>=0&&read(fd,text,sizeof text)==15&&!memcmp(text,"Kunix persisted",15);if(fd>=0)close(fd);CHECK(label,ok);}
static void protections(void){const char* paths[]={"/dev/disk8","/dev/disk17","/dev/disk18"};const char* labels[]={"whole disk raw writes refused","installed root raw writes refused","persistent home raw writes refused"};for(unsigned i=0;i<3;i++){errno=0;int fd=open(paths[i],O_RDWR);CHECK(labels[i],fd<0&&errno==EBUSY);if(fd>=0)close(fd);}errno=0;int fd=open("/dev/disk16",O_RDWR);int open_error=errno;struct kdisk_info info={0};int queried=fd>=0?ioctl(fd,KDISK_INFO,&info):-1;int query_error=errno;if(serial){fprintf(serial,"update EFI: fd=%d open_errno=%d query=%d query_errno=%d flags=%u parent=%u slot=%u block=%u\n",fd,open_error,queried,query_error,info.flags,info.parent,info.slot,info.block_size);fflush(serial);}CHECK("unmounted EFI slice writable",fd>=0&&!queried&&(info.flags&KDISK_ESP));if(fd>=0)close(fd);}
static int network(void){int s=socket(AF_INET,SOCK_DGRAM,0);if(s<0)return 0;struct ifreq req={0};strcpy(req.ifr_name,"eth0");int ready=0;for(unsigned i=0;i<200;i++){if(!ioctl(s,SIOCGIFADDR,&req)&&((struct sockaddr_in*)&req.ifr_addr)->sin_addr.s_addr){ready=1;break;}usleep(100000);}close(s);return ready;}
static int fetch(const char* name,const char* local){char command[256];snprintf(command,sizeof command,"wget -O %s http://10.0.2.2:18084/%s >/dev/serial 2>&1",local,name);return system(command)==0;}
static void reject_sources(void){CHECK("fetch malformed update fixture",fetch("malformed.pkg","/tmp/bad-update.pkg"));CHECK("malformed update refused",system("kunix-update --esp /dev/disk16 /tmp/bad-update.pkg >/dev/serial 2>&1")!=0);CHECK("fetch incomplete update fixture",fetch("incomplete.pkg","/tmp/bad-update.pkg"));CHECK("incomplete update refused",system("kunix-update --esp /dev/disk16 /tmp/bad-update.pkg >/dev/serial 2>&1")!=0);unlink("/tmp/bad-update.pkg");}
int main(int argc,char** argv){serial=fopen("/dev/serial","w");if(argc!=2)return 2;int enroll=!strcmp(argv[1],"enroll"),stage=!strcmp(argv[1],"stage"),rollback=!strcmp(argv[1],"rollback"),verify=!strcmp(argv[1],"verify");if(!enroll&&!stage&&!rollback&&!verify)return 2;
 CHECK("booted expected update generation",generation(enroll?0:rollback?'B':'A'));
 sentinel(enroll?"/phase3-root.txt":"/installed/phase3-root.txt","original installed root sentinel retained");sentinel("/home/phase3-home.txt","persistent home sentinel retained");protections();
 if(enroll||stage){CHECK("DHCP available for update transfer",network());if(!failed){reject_sources();CHECK("download verified update bundle",fetch(stage?"second.pkg":"update.pkg","/home/update-test.pkg"));if(!failed){CHECK("stage and select inactive update generation",system("kunix-update --esp /dev/disk16 /home/update-test.pkg >/dev/serial 2>&1")==0);CHECK("second update before reboot refused",system("kunix-update --esp /dev/disk16 /home/update-test.pkg >/dev/serial 2>&1")!=0);}unlink("/home/update-test.pkg");}}
 if(rollback&&!failed)CHECK("select previous verified generation",system("kunix-update --esp /dev/disk16 --rollback >/dev/serial 2>&1")==0);
 printf("updatetest: %d passed, %d failed\n",passed,failed);if(serial){fprintf(serial,"updatetest: %d passed, %d failed\n",passed,failed);fclose(serial);}return !!failed;
}
