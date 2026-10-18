// This file has been generated in whole or in part by a Large Language Model
#include <types.h>
#include <inet.h>
#include <net_abi.h>
#include <syscall.h>
#include <usermode.h>
#include <proc.h>
#include <socket.h>
#include <memcpy.h>
#include <errno.h>
_Static_assert(sizeof(struct knet_addr)==sizeof(inet_addr_t),"native address layout");
_Static_assert(sizeof(struct knet_info)==sizeof(net_info_t),"native network information layout");
static bool valid(u64 p,u64 n,bool w){return user_range_ok(p,n,w);}
static i64 put_file(file_t* f,u32 flags){i32 fd=fd_install(proc_current(),f,0);if(fd<0){file_unref(f);return fd;}if(flags&KNET_CLOEXEC)proc_current()->fd_cloexec|=1u<<fd;return fd;}
static i32 message_flags(u64 f){if(f&~31ULL)return -EINVAL;return ((f&KNET_PEEK)?SK_MSG_PEEK:0)|((f&KNET_DONTWAIT)?SK_MSG_DONTWAIT:0)|((f&KNET_WAITALL)?SK_MSG_WAITALL:0)|((f&KNET_NOSIGNAL)?SK_MSG_NOSIGNAL:0)|((f&KNET_TRUNC)?SK_MSG_TRUNC:0);}
bool net_native_call(u64 nr,u64 a,u64 b,u64 c,u64 d,u64 e,u64 f6,i64* result){
 if(nr<SYS_SOCKET||nr>SYS_NETINFO)return false;
 i64 r=-EINVAL;
 if(nr==SYS_NETINFO){if(!valid(a,sizeof(net_info_t),true))r=-EFAULT;else{net_info_t info;net_info(&info);memcpy((void*)a,&info,sizeof info);r=0;}goto done;}
 if(nr==SYS_SOCKET){if(a!=KNET_IPV4){r=-97;goto done;}if(d&~3ULL||b>0xffffffffULL||c>0xffffffffULL)goto done;file_t* file;r=inet_create(b,d&KNET_NONBLOCK?O_NONBLOCK:0,c,&file);if(!r)r=put_file(file,d);goto done;}
 file_t* file=fd_get(proc_current(),(i32)a);if(!file){r=-EBADF;goto done;}if(!file_is_inet(file)){r=-88;goto done;}
 switch(nr){
 case SYS_BIND: case SYS_CONNECT:{
  if(!valid(b,sizeof(inet_addr_t),false)){r=-EFAULT;break;}inet_addr_t address;memcpy(&address,(void*)b,sizeof address);
  if(address.reserved){r=-EINVAL;break;}r=nr==SYS_BIND?inet_bind(file,&address):inet_connect(file,&address);break;
 }
 case SYS_LISTEN:r=inet_listen(file,(i32)b);break;
 case SYS_ACCEPT:{
  if(c&~3ULL)break;
  if(b&&!valid(b,sizeof(inet_addr_t),true)){r=-EFAULT;break;}
  inet_addr_t peer;file_t* accepted;r=inet_accept(file,c&KNET_NONBLOCK?O_NONBLOCK:0,&accepted,&peer);
  if(!r){r=put_file(accepted,c);if(r>=0&&b)memcpy((void*)b,&peer,sizeof peer);}break;
 }
 case SYS_SEND: case SYS_RECV:{
  bool receiving=nr==SYS_RECV;i32 flags=message_flags(d);if(flags<0){r=flags;break;}
  if(c>1024*1024){r=-90;break;}if(c&&!valid(b,c,receiving)){r=-EFAULT;break;}
  if(e&&!valid(e,sizeof(inet_addr_t),receiving)){r=-EFAULT;break;}
  if(receiving&&f6&&!valid(f6,4,true)){r=-EFAULT;break;}
  inet_addr_t address={0};u32 out=0;if(!receiving&&e)memcpy(&address,(void*)e,sizeof address);
  if(address.reserved){r=-EINVAL;break;}
  if(receiving){r=inet_recv(file,(void*)b,c,flags,e?&address:NULL,&out);if(r>=0){if(e)memcpy((void*)e,&address,sizeof address);if(f6)*(u32*)f6=(out&SK_MSG_TRUNC)?KNET_TRUNC:0;}}
  else r=inet_send(file,(void*)b,c,flags,e?&address:NULL);
  break;
 }
 case SYS_SHUTDOWN:r=inet_shutdown(file,(i32)b);break;
 case SYS_SOCKNAME:{if(!valid(b,sizeof(inet_addr_t),true)){r=-EFAULT;break;}inet_addr_t address;r=inet_name(file,&address,c!=0);if(!r)memcpy((void*)b,&address,sizeof address);break;}
 case SYS_SOCKOPT:{
  if(b<1||b>INET_BROADCAST){r=-92;break;}
  if(e>1)break;
  if(!valid(d,4,!e)){r=-EFAULT;break;}u32 length=*(u32*)d;u64 storage=0;u8* value=(u8*)&storage;
  if(e&&length>sizeof storage){r=-EINVAL;break;}u32 copy=length<sizeof storage?length:sizeof storage;
  if(copy&&!valid(c,copy,!e)){r=-EFAULT;break;}if(e)memcpy(value,(void*)c,copy);
  u32 actual=e?length:sizeof storage;r=inet_option(file,e,b,value,&actual);
  if(!r&&!e){if(actual>sizeof storage){r=-EIO;break;}if(actual<copy)copy=actual;memcpy((void*)c,value,copy);*(u32*)d=actual;}break;
 }
 }
 done:*result=r;return true;
}
