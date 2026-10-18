// This file has been generated in whole or in part by a Large Language Model
// Bootstrap an existing installation from live media without network or reinstall.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "../../../include/disk_ioctl.h"
static int passed,failed;static FILE *serial;
#define CHECK(name,expr) do{int check_result=!!(expr);passed+=check_result;failed+=!check_result;printf("%s %s\n",check_result?"PASS":"FAIL",name);if(serial){fprintf(serial,"%s %s\n",check_result?"PASS":"FAIL",name);fflush(serial);}}while(0)
static void sentinel(const char *path){char text[32]={0};int fd=open(path,O_RDONLY);int ok=fd>=0&&read(fd,text,sizeof text)==15&&!memcmp(text,"Kunix persisted",15);if(fd>=0)close(fd);CHECK(path,ok);}
int main(int argc,char **argv){
 serial=fopen("/dev/serial","w");if(argc!=2)return 2;
 int stage=!strcmp(argv[1],"stage"),verify=!strcmp(argv[1],"verify");if(!stage&&!verify)return 2;
 CHECK("live bootstrap root",geteuid()==0);
 char line[4096]={0};int fd=open("/proc/cmdline",O_RDONLY);ssize_t n=fd>=0?read(fd,line,sizeof(line)-1):-1;if(fd>=0)close(fd);
 CHECK("live bootstrap expected generation",n>=0&&(stage?!strstr(line,"generation="):strstr(line,"generation=A")!=NULL));
 if(stage){
  fd=open("/dev/install-source",O_RDONLY);struct kdisk_info info={0};
  CHECK("live bootstrap pristine module source",fd>=0&&!ioctl(fd,KDISK_INFO,&info)&&(info.flags&KDISK_RAM)&&(info.flags&KDISK_READONLY));if(fd>=0)close(fd);
  CHECK("live bootstrap installed home unmounted",access("/home/phase3-home.txt",F_OK)!=0);
  /* This dummy overlay file must never be captured into the next root image. */
  fd=open("/etc/live-update-overlay-sentinel",O_CREAT|O_EXCL|O_WRONLY,0600);
  CHECK("live bootstrap disposable overlay marker",fd>=0&&write(fd,"overlay-only",12)==12);if(fd>=0)close(fd);
  if(!failed)CHECK("live bootstrap stages without network or home",system("kunix-update --esp /dev/disk16 --from-live >/dev/serial 2>&1")==0);
 }else{
  sentinel("/installed/phase3-root.txt");sentinel("/home/phase3-home.txt");
  CHECK("live overlay excluded from staged generation",access("/etc/live-update-overlay-sentinel",F_OK)!=0);
  CHECK("live bootstrap updater included",access("/bin/kunix-update",X_OK)==0);
 }
 sync();printf("liveupdatetest: %d passed, %d failed\n",passed,failed);if(serial){fprintf(serial,"liveupdatetest: %d passed, %d failed\n",passed,failed);fclose(serial);}return !!failed;
}
