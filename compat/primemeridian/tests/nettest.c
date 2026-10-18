// This file has been generated in whole or in part by a Large Language Model
// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/sendfile.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/vfs.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/sendfile.h>
#include <signal.h>
static int passed,failed; static FILE* serial;
#define CHECK(name,expr) do { int ok=!!(expr);passed+=ok;failed+=!ok;printf("%s %s\n",ok?"PASS":"FAIL",name);if(serial){fprintf(serial,"%s %s\n",ok?"PASS":"FAIL",name);fflush(serial);} }while(0)
static struct sockaddr_in loop(void){struct sockaddr_in a={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};return a;}
static void deadline(int fd){struct timeval t={.tv_sec=2};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof t);setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof t);}
static void udp(void){
 int a=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0),b=socket(AF_INET,SOCK_DGRAM,0);CHECK("udp sockets",a>=0&&b>=0);if(a<0||b<0)goto done;
 struct stat st;CHECK("socket fstat mode",fstat(a,&st)==0&&S_ISSOCK(st.st_mode));
 struct statfs filesystem;CHECK("socket filesystem stat",fstatfs(a,&filesystem)==0&&filesystem.f_type==0x534f434b);
 deadline(a);deadline(b);struct sockaddr_in addr=loop(),from;socklen_t n=sizeof addr;
 int type=0;socklen_t olen=sizeof type;CHECK("udp SO_TYPE",getsockopt(a,SOL_SOCKET,SO_TYPE,&type,&olen)==0&&type==SOCK_DGRAM&&olen==4);
 CHECK("udp bind",bind(a,(void*)&addr,sizeof addr)==0);CHECK("udp ephemeral name",getsockname(a,(void*)&addr,&n)==0&&n==16&&addr.sin_port!=0);
 CHECK("udp sendto",sendto(b,"hello",5,0,(void*)&addr,sizeof addr)==5);
 char x[16]={0};n=sizeof from;CHECK("recv validates address before consume",recvfrom(a,x,sizeof x,0,(void*)1,&n)==-1&&errno==EFAULT);
 CHECK("udp recvfrom",recvfrom(a,x,sizeof x,0,(void*)&from,&n)==5&&!memcmp(x,"hello",5)&&from.sin_family==AF_INET);
 struct iovec iov[2]={{"ab",2},{"cd",2}};struct msghdr m={.msg_name=&addr,.msg_namelen=sizeof addr,.msg_iov=iov,.msg_iovlen=2};CHECK("udp sendmsg gather",sendmsg(b,&m,0)==4);
 char p[2],q[2];iov[0]=(struct iovec){p,2};iov[1]=(struct iovec){q,2};m.msg_name=&from;m.msg_namelen=sizeof from;
 CHECK("udp recvmsg scatter and name",recvmsg(a,&m,0)==4&&!memcmp(p,"ab",2)&&!memcmp(q,"cd",2)&&m.msg_namelen==16&&from.sin_port!=0);
 CHECK("udp datagram truncation send",sendto(b,"abcdef",6,0,(void*)&addr,sizeof addr)==6);
 CHECK("udp peek truncation",recv(a,x,2,MSG_PEEK|MSG_TRUNC)==6&&!memcmp(x,"ab",2));
 CHECK("udp datagram remains after peek",recv(a,x,sizeof x,0)==6&&!memcmp(x,"abcdef",6));
 CHECK("udp nonblock empty",recv(a,x,1,MSG_DONTWAIT)==-1&&(errno==EAGAIN||errno==EWOULDBLOCK));
 struct timeval t={.tv_usec=20000};CHECK("receive timeout set",setsockopt(a,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof t)==0);struct timeval got={0};olen=sizeof got;CHECK("receive timeout get",getsockopt(a,SOL_SOCKET,SO_RCVTIMEO,&got,&olen)==0&&olen==sizeof got&&got.tv_sec==0&&got.tv_usec==20000);CHECK("receive timeout",recv(a,x,1,0)==-1&&errno==EAGAIN);
 t.tv_usec=1000000;CHECK("invalid timeout",setsockopt(a,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof t)==-1&&errno==EDOM);
 CHECK("short sockaddr",bind(b,(void*)&addr,2)==-1&&errno==EINVAL);
 CHECK("wrapped user range",sendto(b,(void*)(uintptr_t)(UINTPTR_MAX-1),4,0,(void*)&addr,16)==-1&&errno==EFAULT);
 CHECK("bad address",sendto(b,"x",1,0,(void*)1,16)==-1&&errno==EFAULT);
 int v=1;CHECK("unknown option",setsockopt(a,SOL_SOCKET,0x7fff,&v,sizeof v)==-1&&errno==ENOPROTOOPT);
 CHECK("bad option pointer",setsockopt(a,SOL_SOCKET,SO_REUSEADDR,(void*)1,4)==-1&&errno==EFAULT);
 iov[0]=(struct iovec){x,SIZE_MAX};iov[1]=(struct iovec){x,8};m.msg_name=NULL;CHECK("iov overflow bounded",sendmsg(b,&m,0)==-1&&errno==EMSGSIZE);
 done:if(a>=0)close(a);if(b>=0)close(b);
}
static void icmp(void){
 int fd=socket(AF_INET,SOCK_RAW|SOCK_CLOEXEC,IPPROTO_ICMP);
 CHECK("raw ICMP socket",fd>=0);if(fd<0)return;
 deadline(fd);int type=0,ttl=42;socklen_t n=sizeof type;
 CHECK("raw SO_TYPE",getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&n)==0&&type==SOCK_RAW);
 CHECK("raw IP_TTL",setsockopt(fd,IPPROTO_IP,IP_TTL,&ttl,sizeof ttl)==0);
 ttl=0;CHECK("raw invalid TTL",setsockopt(fd,IPPROTO_IP,IP_TTL,&ttl,sizeof ttl)==-1&&errno==EINVAL);
 struct sockaddr_in addr=loop(),from;
 CHECK("raw bind",bind(fd,(void*)&addr,sizeof addr)==0);
 unsigned char request[8]={8,0,0xf7,0xfd,0,1,0,1},reply[64];
 CHECK("raw echo send",sendto(fd,request,sizeof request,0,(void*)&addr,sizeof addr)==sizeof request);
 int got=0,source=0;for(unsigned i=0;i<3;i++){
  n=sizeof from;ssize_t count=recvfrom(fd,reply,sizeof reply,0,(void*)&from,&n);
  if(count<0)break;
  if(count>=28 && (reply[0]>>4)==4 && reply[9]==IPPROTO_ICMP){
   unsigned off=(reply[0]&15)*4;
   if(off+8<=(unsigned)count && reply[off]==0 && reply[off+4]==0 && reply[off+5]==1){got=1;source=n==16&&from.sin_family==AF_INET&&from.sin_addr.s_addr==addr.sin_addr.s_addr&&from.sin_port==0;break;}
  }
 }
 CHECK("raw IPv4 echo reply",got);CHECK("raw receive source address",source);close(fd);
 pid_t child=fork();CHECK("raw privilege test fork",child>=0);
 if(child==0){if(setuid(65534))_exit(1);int raw=socket(AF_INET,SOCK_RAW,IPPROTO_ICMP);_exit(raw<0&&errno==EPERM?0:2);}
 if(child>0){int status=0;CHECK("raw socket requires root",waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);}
 CHECK("BusyBox ping loopback",system("ping -c 1 -W 2 -t 42 127.0.0.1 >/dev/serial 2>&1")==0);
}
static void interfaces(void){
 int s=socket(AF_INET,SOCK_DGRAM,0);if(s<0)return;
 struct ifconf conf={0};CHECK("interface size query",ioctl(s,SIOCGIFCONF,&conf)==0&&conf.ifc_len>=(int)sizeof(struct ifreq));
 struct ifreq entries[4];conf.ifc_buf=(char*)entries;conf.ifc_len=sizeof entries;
 CHECK("interface list",ioctl(s,SIOCGIFCONF,&conf)==0&&conf.ifc_len>=(int)sizeof(struct ifreq)&&!strcmp(entries[0].ifr_name,"lo")&&((struct sockaddr_in*)&entries[0].ifr_addr)->sin_addr.s_addr==htonl(INADDR_LOOPBACK));
 struct ifreq req={0};strcpy(req.ifr_name,"lo");CHECK("loopback flags",ioctl(s,SIOCGIFFLAGS,&req)==0&&(req.ifr_flags&(IFF_UP|IFF_LOOPBACK|IFF_RUNNING))==(IFF_UP|IFF_LOOPBACK|IFF_RUNNING));
 CHECK("loopback hardware",ioctl(s,SIOCGIFHWADDR,&req)==0&&req.ifr_hwaddr.sa_family==772&&!memcmp(req.ifr_hwaddr.sa_data,"\0\0\0\0\0\0",6));
 CHECK("loopback index",ioctl(s,SIOCGIFINDEX,&req)==0&&req.ifr_ifindex==1);
 memset(&req,0,sizeof req);req.ifr_ifindex=1;CHECK("interface index name",ioctl(s,SIOCGIFNAME,&req)==0&&!strcmp(req.ifr_name,"lo"));
 strcpy(req.ifr_name,"missing");CHECK("unknown interface",ioctl(s,SIOCGIFFLAGS,&req)==-1&&errno==ENODEV);
 close(s);
}
static void tcp(void){
 int s=socket(AF_INET,SOCK_STREAM,0);CHECK("tcp socket",s>=0);if(s<0)return;deadline(s);struct sockaddr_in a=loop();socklen_t n=sizeof a;int one=1;
 CHECK("tcp reuseaddr",setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&one,4)==0);CHECK("tcp bind listen",bind(s,(void*)&a,sizeof a)==0&&listen(s,4)==0&&getsockname(s,(void*)&a,&n)==0);
 int child=fork();if(child==0){int c=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK,0);if(c<0)_exit(1);int r=connect(c,(void*)&a,sizeof a);if(r<0&&errno!=EINPROGRESS)_exit(2);struct pollfd p={c,POLLOUT,0};if(poll(&p,1,2000)!=1)_exit(3);int err=-1;socklen_t z=4;if(getsockopt(c,SOL_SOCKET,SO_ERROR,&err,&z)||err)_exit(4);fcntl(c,F_SETFL,0);deadline(c);if(write(c,"ping",4)!=4||shutdown(c,SHUT_WR))_exit(5);char b[4];if(read(c,b,4)!=4||memcmp(b,"pong",4))_exit(6);close(c);_exit(0);}
 CHECK("tcp fork",child>=0);if(child>0){struct pollfd p={s,POLLIN,0};CHECK("listener poll",poll(&p,1,2000)==1&&(p.revents&POLLIN));n=sizeof a;
 CHECK("accept validates before consume",accept4(s,(void*)1,&n,0)==-1&&errno==EFAULT);
 int c=accept4(s,(void*)&a,&n,SOCK_CLOEXEC);CHECK("tcp accept4",c>=0&&n==16);if(c>=0){deadline(c);int opt=1;CHECK("TCP_NODELAY",setsockopt(c,IPPROTO_TCP,TCP_NODELAY,&opt,sizeof opt)==0);CHECK("SO_KEEPALIVE",setsockopt(c,SOL_SOCKET,SO_KEEPALIVE,&opt,sizeof opt)==0);n=sizeof a;CHECK("TCP peer name",getpeername(c,(void*)&a,&n)==0&&n==16&&a.sin_family==AF_INET&&a.sin_port!=0);char b[4];CHECK("tcp read",read(c,b,4)==4&&!memcmp(b,"ping",4));CHECK("tcp half close EOF",read(c,b,4)==0);CHECK("tcp reply after peer half close",write(c,"pong",4)==4);CHECK("accept cloexec",fcntl(c,F_GETFD)&FD_CLOEXEC);close(c);}int status=0;CHECK("tcp nonblocking child",waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);}
 close(s);
}
/* Host harness: TCP 18080 echoes bytes until EOF; UDP 18081 echoes each datagram.
 * QEMU user networking exposes both on 10.0.2.2. DNS uses its normal resolver. */
#define EXTERNAL_DIAG(...) do { printf(__VA_ARGS__);if(serial){fprintf(serial,__VA_ARGS__);fflush(serial);} }while(0)
static void external(void){
 struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons(18080)};
 inet_pton(AF_INET,"10.0.2.2",&a.sin_addr);
 int c=socket(AF_INET,SOCK_STREAM,0);
 EXTERNAL_DIAG("external TCP socket=%d errno=%d\n",c,errno);
 struct ifreq req={0};strcpy(req.ifr_name,"eth0");
 int config=-1,assigned=0;char ip[INET_ADDRSTRLEN]={0};
 /* DHCP may have supplied DNS before address conflict detection completes. */
 for(unsigned attempt=0;c>=0&&attempt<150;attempt++){
  config=ioctl(c,SIOCGIFADDR,&req);
  if(config==0&&((struct sockaddr_in*)&req.ifr_addr)->sin_addr.s_addr!=INADDR_ANY){assigned=1;break;}
  usleep(100000);
 }
 if(config==0)inet_ntop(AF_INET,&((struct sockaddr_in*)&req.ifr_addr)->sin_addr,ip,sizeof ip);
 EXTERNAL_DIAG("external eth0 address result=%d errno=%d ip=%s\n",config,errno,ip);
 CHECK("DHCP assigned IPv4 address",assigned);
 if(!assigned){if(c>=0)close(c);return;}
 deadline(c);
 char b[8]={0};int connected=-1;ssize_t sent=-1,received=-1;
 if(c>=0){connected=connect(c,(void*)&a,sizeof a);EXTERNAL_DIAG("external TCP connect=%d errno=%d\n",connected,errno);}
 if(connected==0){sent=write(c,"network",7);EXTERNAL_DIAG("external TCP write=%ld errno=%d\n",(long)sent,errno);}
 if(sent==7){received=read(c,b,7);EXTERNAL_DIAG("external TCP read=%ld errno=%d\n",(long)received,errno);}
 CHECK("external TCP echo",c>=0&&connected==0&&sent==7&&received==7&&!memcmp(b,"network",7));
 if(c>=0)close(c);
 a.sin_port=htons(18081);c=socket(AF_INET,SOCK_DGRAM,0);
 EXTERNAL_DIAG("external UDP socket=%d errno=%d\n",c,errno);
 if(c>=0)deadline(c);
 sent=received=-1;
 if(c>=0){sent=sendto(c,"udp",3,0,(void*)&a,sizeof a);EXTERNAL_DIAG("external UDP send=%ld errno=%d\n",(long)sent,errno);}
 if(sent==3){received=recv(c,b,8,0);EXTERNAL_DIAG("external UDP recv=%ld errno=%d\n",(long)received,errno);}
 CHECK("external UDP echo",c>=0&&sent==3&&received==3&&!memcmp(b,"udp",3));
 if(c>=0)close(c);
 CHECK("busybox HTTP fetch",system("wget -O /tmp/net-http http://10.0.2.2:18083/test >/dev/serial 2>&1")==0);
 int http=open("/tmp/net-http",O_RDONLY);char payload[32]={0};
 CHECK("HTTP payload intact",http>=0&&read(http,payload,sizeof(payload))==14&&!memcmp(payload,"kunix-http-ok\n",14));
 if(http>=0)close(http);
 struct addrinfo* res=NULL;int r=getaddrinfo("example.com",NULL,NULL,&res);
 EXTERNAL_DIAG("external DNS result=%d errno=%d message=%s\n",r,errno,gai_strerror(r));
 CHECK("external DNS",r==0&&res!=NULL);if(res)freeaddrinfo(res);
}

static unsigned long long cycles(void){unsigned lo,hi;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));return (unsigned long long)hi<<32|lo;}
static void busy_external(void){
 int ready[2];CHECK("CPU load synchronization pipe",pipe(ready)==0);if(failed)return;
  pid_t child=fork();
  if(child==0){close(ready[0]);(void)write(ready[1],"r",1);unsigned long long start=cycles();while(cycles()-start<8000000000ULL)__asm__ volatile("pause");_exit(0);}
  CHECK("CPU load process",child>=0);
 close(ready[1]);if(child<0){close(ready[0]);return;}char marker=0;(void)read(ready[0],&marker,1);close(ready[0]);
 struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons(18080),.sin_addr.s_addr=htonl(0x0a000202)};
 int fd=socket(AF_INET,SOCK_STREAM,0);char data[4]={0};if(fd>=0)deadline(fd);
 CHECK("Ethernet RX progresses during CPU-only user code",marker=='r'&&fd>=0&&connect(fd,(void*)&a,sizeof a)==0&&write(fd,"busy",4)==4&&read(fd,data,4)==4&&!memcmp(data,"busy",4)&&waitpid(child,NULL,WNOHANG)==0);
 if(fd>=0)close(fd);
 kill(child,SIGKILL);(void)waitpid(child,NULL,0);
}
static void descriptor_errors(void){
 int fd=socket(AF_INET,SOCK_STREAM,0);CHECK("socket descriptor for invalid file operations",fd>=0);if(fd<0)return;
 char data[256];off_t offset=0;
 CHECK("socket lseek is ESPIPE",lseek(fd,0,SEEK_SET)==-1&&errno==ESPIPE);
 CHECK("socket pread is ESPIPE",pread(fd,data,1,0)==-1&&errno==ESPIPE);
 CHECK("socket pwrite is ESPIPE",pwrite(fd,"x",1,0)==-1&&errno==ESPIPE);
 CHECK("socket sendfile offset is ESPIPE",sendfile(fd,fd,&offset,1)==-1&&errno==ESPIPE);
 CHECK("socket fchdir is ENOTDIR",fchdir(fd)==-1&&errno==ENOTDIR);
 CHECK("socket getdents is ENOTDIR",syscall(SYS_getdents64,fd,data,sizeof data)==-1&&errno==ENOTDIR);
 CHECK("socket ftruncate is EINVAL",ftruncate(fd,0)==-1&&errno==EINVAL);
 CHECK("socket fchmod unsupported",fchmod(fd,0600)==-1&&errno==EOPNOTSUPP);
 CHECK("socket fchown unsupported",fchown(fd,0,0)==-1&&errno==EOPNOTSUPP);
 close(fd);
}

int main(int argc,char**argv){serial=fopen("/dev/serial","w");CHECK("IPv6 unsupported",socket(AF_INET6,SOCK_STREAM,0)==-1&&errno==EAFNOSUPPORT);udp();interfaces();icmp();tcp();if(argc>1&&!strcmp(argv[1],"external")){external();busy_external();}descriptor_errors();printf("nettest: %d passed, %d failed\n",passed,failed);if(serial){fprintf(serial,"nettest: %d passed, %d failed\n",passed,failed);fclose(serial);}return failed?1:0;}
