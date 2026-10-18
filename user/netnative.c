// This file has been generated in whole or in part by a Large Language Model
#include "kunix.h"
#include "../include/net_abi.h"
// Literal syscall values deliberately check the published native ABI contract.
static int passed,failed,serial;
static void check(const char* name,int ok){passed+=!!ok;failed+=!ok;printf("%s %s\n",ok?"PASS":"FAIL",name);char line[160];snprintf(line,sizeof line,"%s %s\n",ok?"PASS":"FAIL",name);write(serial,line,strlen(line));}
int main(void){serial=open("/dev/serial",O_WRONLY,0);struct knet_info info;
 check("native netinfo",syscall3(58,(long)&info,0,0)==0&&info.mtu>=1500);
 check("native bad netinfo pointer",syscall3(58,1,0,0)==-EFAULT);
 check("native domain is independent",syscall6(48,2,KNET_DGRAM,0,0,0,0)==-97);
 long a=syscall6(48,KNET_IPV4,KNET_DGRAM,0,0,0,0),b=syscall6(48,KNET_IPV4,KNET_DGRAM,0,0,0,0);
 check("native UDP sockets",a>=0&&b>=0);
 if(a>=0&&b>=0){struct stat st;check("native socket stat",fstat(a,&st)==0&&(st.st_mode&0170000)==0140000);check("native socket seek is ESPIPE",lseek(a,0,SEEK_SET)==-1&&errno==ESPIPE);struct kdirent entry;check("native socket readdir is ENOTDIR",readdir_fd(a,&entry)==-1&&errno==ENOTDIR);struct knet_addr addr={.address=0x0100007f};char data[8]={0};
 check("native UDP bind",syscall3(49,a,(long)&addr,0)==0);
 check("native socket name",syscall3(56,a,(long)&addr,0)==0&&addr.port!=0);
 check("native UDP send",syscall6(53,b,(long)"kunix",5,0,(long)&addr,0)==5);
 check("native recv validates pointer",syscall6(54,a,1,5,KNET_DONTWAIT,0,0)==-EFAULT);
 check("native UDP recv",syscall6(54,a,(long)data,8,0,0,0)==5&&!strcmp(data,"kunix"));
 check("native nonblocking empty",syscall6(54,a,(long)data,8,KNET_DONTWAIT,0,0)==-EAGAIN);
 }if(a>=0)close(a);if(b>=0)close(b);
 printf("netnative: %d passed, %d failed\n",passed,failed);char line[96];snprintf(line,sizeof line,"netnative: %d passed, %d failed\n",passed,failed);write(serial,line,strlen(line));close(serial);return !!failed;
}
