// This file has been generated in whole or in part by a Large Language Model
// Run with buildutils/tests/battery_table.py's SSDT supplied to QEMU.
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/klog.h>
#include <sys/wait.h>
#include <unistd.h>
static FILE* serial;
static int passed,failed;
#define CHECK(name,expr) do { int ok=(expr); passed+=ok;failed+=!ok; \
 printf("%s %s\n",ok?"PASS":"FAIL",name); \
 if(serial) {fprintf(serial,"%s %s\n",ok?"PASS":"FAIL",name);fflush(serial);} } while(0)
int main(int argc,char** argv) {
 int low=argc>1 && !strcmp(argv[1],"low");
 serial=fopen("/dev/serial","w");
 char buf[4096]={0};
 for(int i=0;i<50;i++) {
  int fd=open("/proc/battery",O_RDONLY);
  ssize_t n=fd>=0?read(fd,buf,sizeof(buf)-1):-1;
  if(fd>=0)close(fd);
  if(n>0)buf[n]=0;
  if(strstr(buf,"status: ready"))break;
  usleep(100000);
 }
 CHECK("firmware battery backend is ready",strstr(buf,"status: ready")!=NULL);
 CHECK("AML battery and AC device discovery",strstr(buf,"batteries: 1") && strstr(buf,"ac: offline"));
 CHECK("firmware charge and state",strstr(buf,low?"percent: 5\n":"percent: 50\n") && strstr(buf,"state: discharging"));
 CHECK("firmware capacity units",strstr(buf,"unit: mWh/mW") && strstr(buf,"full: 50000"));
 pid_t p=fork();
 if(p==0) {execl("/bin/battery","battery",NULL);_exit(1);}
 int st=0; waitpid(p,&st,0);
 CHECK("native Kunix battery command runs",p>0 && WIFEXITED(st) && WEXITSTATUS(st)==0);
 if(low) {
  char log[65537];int n=klogctl(3,log,65536);if(n>=0)log[n]=0;
  CHECK("low battery warning reaches kernel log",n>0 && strstr(log,"battery0: low battery (5%)"));
 }
 printf("powertest: %d passed, %d failed\n",passed,failed);
 if(serial){fprintf(serial,"powertest: %d passed, %d failed\n",passed,failed);fclose(serial);}
 return failed!=0;
}
