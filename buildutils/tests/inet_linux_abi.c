// This file has been generated in whole or in part by a Large Language Model
// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
/* cc -fno-builtin -Wall -Wextra -Werror -Iinclude -Icompat/primemeridian
 * buildutils/tests/inet_linux_abi.c -o /tmp/inet-linux-abi && /tmp/inet-linux-abi
 * The mock user arena rejects every kernel stack/heap address. */
#include "../../compat/primemeridian/inet.c"
extern void* malloc(size_t);
extern void free(void*);
extern int printf(const char*,...);
extern void abort(void);
static u8 user[4096];
static process_t process;
static file_t internet,other;
static int calls,checks;
static bool truncated;
static u64 timeout_value;
static u32 selected_option;
static net_info_t network;
void net_info(net_info_t* out){*out=network;}
void net_ifname(char out[16]){const char* n="eth0";for(int i=0;i<16;i++){out[i]=n[i];if(!n[i])break;}}
#define CHECK(x) do{checks++;if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);abort();}}while(0)
void* kmalloc(uint64_t n){return malloc(n);}
void kfree(void* p){free(p);}
process_t* proc_current(void){return &process;}
file_t* fd_get(process_t* p,i32 fd){(void)p;return fd==3?&internet:fd==4?&other:NULL;}
i32 fd_install(process_t* p,file_t* f,i32 low){(void)p;(void)f;(void)low;return 5;}
void file_unref(file_t* f){(void)f;}
bool user_range_ok(u64 p,u64 n,bool write){(void)write;u64 lo=(u64)user;return p>=lo&&p<=lo+sizeof user&&n<=lo+sizeof user-p;}
bool file_is_inet(const file_t* f){return f==&internet;}
i32 inet_create(u32 type,u32 flags,u32 protocol,file_t** out){CHECK(type==1);CHECK(flags==O_NONBLOCK);CHECK(protocol==6);*out=&internet;return 0;}
i32 inet_bind(file_t* f,const inet_addr_t* a){(void)f;CHECK(a->address==0x0100007f&&a->port==0x1234);calls++;return 0;}
i32 inet_connect(file_t* f,const inet_addr_t* a){return inet_bind(f,a);}
i32 inet_listen(file_t* f,i32 n){(void)f;(void)n;calls++;return 0;}
i32 inet_accept(file_t* f,u32 flags,file_t** out,inet_addr_t* a){(void)f;(void)flags;calls++;*out=&internet;*a=(inet_addr_t){.address=0x0100007f,.port=0x1234};return 0;}
i32 inet_shutdown(file_t* f,i32 how){(void)f;(void)how;calls++;return 0;}
i32 inet_name(file_t* f,inet_addr_t* a,bool peer){(void)f;(void)peer;*a=(inet_addr_t){.address=0x0100007f,.port=0x1234};return 0;}
i64 inet_send(file_t* f,const void* b,u64 n,u32 flags,const inet_addr_t* a){(void)f;(void)flags;(void)a;calls++;CHECK(!user_range_ok((u64)b,n,false));return n;}
i64 inet_recv(file_t* f,void* b,u64 n,u32 flags,inet_addr_t* a,u32* of){(void)f;(void)flags;calls++;CHECK(!user_range_ok((u64)b,n,true));if(n)((char*)b)[0]='x';*a=(inet_addr_t){.address=0x0100007f,.port=0x1234};*of=32;return truncated?(i64)n+4:n?1:0;}
i32 inet_option(file_t* f,bool set,u32 opt,void* value,u32* length){(void)f;(void)length;calls++;selected_option=opt;if(set)timeout_value=*(u64*)value;else *(u64*)value=timeout_value;return 0;}
static i64 call(u64 nr,u64 a,u64 b,u64 c,u64 d,u64 e,u64 f){i64 r=-999;CHECK(pm_inet_call(nr,a,b,c,d,e,f,&r));return r;}
static i64 ioctl_call(u32 req,u64 p){i64 r=-999;CHECK(pm_inet_ioctl(&internet,req,p,&r));return r;}
static void interface_tests(void){
 i64 r;CHECK(!pm_inet_ioctl(&other,0x8912,(u64)user,&r));CHECK(!pm_inet_ioctl(&internet,0x8914,(u64)user,&r));
 interfaces_t* conf=(interfaces_t*)(user+1024);*conf=(interfaces_t){0};
 CHECK(ioctl_call(0x8912,(u64)conf)==0&&conf->length==40);
 CHECK(ioctl_call(0x8912,1)==-EFAULT);
 conf->length=-1;CHECK(ioctl_call(0x8912,(u64)conf)==-EINVAL);
 conf->length=39;conf->buffer=1;CHECK(ioctl_call(0x8912,(u64)conf)==0&&conf->length==0);
 conf->length=40;conf->buffer=1;CHECK(ioctl_call(0x8912,(u64)conf)==-EFAULT);
 interface_t* entry=(interface_t*)(user+1088);conf->buffer=(u64)entry;
 CHECK(ioctl_call(0x8912,(u64)conf)==0&&conf->length==40&&interface_name(entry->name,"lo"));
 address_t* addr=(address_t*)entry->data;CHECK(addr->family==2&&addr->address==0x0100007f);
 CHECK(ioctl_call(0x8913,(u64)entry)==0&&*(u16*)entry->data==(1|8|64));
 CHECK(ioctl_call(0x8921,(u64)entry)==0&&*(i32*)entry->data==65536);
 CHECK(ioctl_call(0x8927,(u64)entry)==0&&*(u16*)entry->data==772&&entry->data[2]==0);
 CHECK(ioctl_call(0x8933,(u64)entry)==0&&*(i32*)entry->data==1);
 entry->data[0]=2;CHECK(ioctl_call(0x8910,(u64)entry)==-ENODEV);
 entry->data[0]=1;CHECK(ioctl_call(0x8910,(u64)entry)==0&&interface_name(entry->name,"lo"));
 interface_address(entry,"eth0",0);CHECK(ioctl_call(0x8913,(u64)entry)==-ENODEV);
 network=(net_info_t){.mtu=1500,.address=0x0f02000a,.netmask=0x00ffffff,.up=1,.link=0,.mac={0x52,0x54,0,1,2,3}};
 CHECK(ioctl_call(0x8913,(u64)entry)==0&&*(u16*)entry->data==(1|2|4096));
 CHECK(ioctl_call(0x8927,(u64)entry)==0&&*(u16*)entry->data==1&&entry->data[2]==0x52&&entry->data[7]==3);
 CHECK(ioctl_call(0x8915,(u64)entry)==0&&((address_t*)entry->data)->address==network.address);
 CHECK(ioctl_call(0x891b,(u64)entry)==0&&((address_t*)entry->data)->address==network.netmask);
 CHECK(ioctl_call(0x8919,(u64)entry)==0&&((address_t*)entry->data)->address==(network.address|~network.netmask));
 *conf=(interfaces_t){.length=80,.buffer=(u64)entry};CHECK(ioctl_call(0x8912,(u64)conf)==0&&conf->length==80&&interface_name(entry[1].name,"eth0"));
 network.address=0;conf->length=80;CHECK(ioctl_call(0x8912,(u64)conf)==0&&conf->length==40);
 CHECK(ioctl_call(0x8913,(u64)(user+sizeof user-39))==-EFAULT);
}
int main(void){
 interface_tests();
 i64 r;CHECK(!pm_inet_call(41,1,1,0,0,0,0,&r));CHECK(!pm_inet_call(44,4,0,0,0,0,0,&r));CHECK(!pm_inet_call(44,90,0,0,0,0,0,&r));CHECK(!pm_inet_call(1,3,0,0,0,0,0,&r));
 CHECK(call(41,10,1,0,0,0,0)==-97);CHECK(call(41,2,1|SOCK_NONBLOCK|SOCK_CLOEXEC,6,0,0,0)==5);CHECK(process.fd_cloexec&(1u<<5));CHECK(call(41,2,0x100,0,0,0,0)==-EINVAL);
 address_t* a=(address_t*)user;*a=(address_t){.family=2,.port=0x1234,.address=0x0100007f};
 CHECK(call(49,3,(u64)a,2,0,0,0)==-EINVAL);CHECK(call(49,3,1,16,0,0,0)==-EFAULT);CHECK(call(49,3,(u64)a,16,0,0,0)==0);
 u32* len=(u32*)(user+32);*len=16;int before=calls;CHECK(call(43,3,1,(u64)len,0,0,0)==-EFAULT&&calls==before);CHECK(call(45,3,(u64)(user+64),8,0,1,(u64)len)==-EFAULT&&calls==before);
 CHECK(call(43,3,(u64)a,(u64)len,0,0,0)==5&&*len==16&&a->port==0x1234);
 *len=1;CHECK(call(51,3,(u64)a,(u64)len,0,0,0)==0&&*len==16);
 message_t* m=(message_t*)(user+128);l_iovec_t* iov=(l_iovec_t*)(user+256);*m=(message_t){.name=(u64)a,.namelen=16,.iov=(u64)iov,.iovlen=2};iov[0]=(l_iovec_t){.iov_base=(u64)(user+64),.iov_len=2};iov[1]=(l_iovec_t){.iov_base=(u64)(user+80),.iov_len=2};
 CHECK(call(47,3,(u64)m,0,0,0,0)==1);CHECK(user[64]=='x'&&m->namelen==16&&a->port==0x1234&&m->flags==32&&m->controllen==0);
 m->name=1;before=calls;CHECK(call(47,3,(u64)m,0,0,0,0)==-EFAULT&&calls==before);m->name=(u64)a;
 truncated=true;user[66]='z';user[82]='z';CHECK(call(47,3,(u64)m,32,0,0,0)==8&&user[66]=='z'&&user[82]=='z');truncated=false;
 iov[0].iov_len=~0ULL;before=calls;CHECK(call(46,3,(u64)m,0,0,0,0)==-EMSGSIZE&&calls==before);
 iov[0].iov_len=1;m->control=(u64)(user+320);m->controllen=16;CHECK(call(46,3,(u64)m,0,0,0,0)==-EINVAL&&calls==before);
 timeval_t* t=(timeval_t*)(user+384);*t=(timeval_t){.sec=1,.usec=1};CHECK(call(54,3,1,20,(u64)t,16,0)==0&&timeout_value==1001&&selected_option==INET_RCVTIMEO);
 t->sec=0x7fffffffffffffffLL;CHECK(call(54,3,1,20,(u64)t,16,0)==-ERANGE);t->sec=0;t->usec=1000000;CHECK(call(54,3,1,20,(u64)t,16,0)==-EDOM);
 *len=16;timeout_value=1001;CHECK(call(55,3,1,20,(u64)t,(u64)len,0)==0&&t->sec==1&&t->usec==1000&&*len==16);
 CHECK(call(54,3,1,65535,(u64)t,4,0)==-ENOPROTOOPT);before=calls;CHECK(call(55,3,1,4,1,(u64)len,0)==-EFAULT&&calls==before);
 CHECK(call(44,3,(u64)user,LIMIT+1,0,0,0)==-EMSGSIZE);CHECK(call(44,3,(u64)(user+64),4,0,0,0)==4);
 printf("inet Linux ABI: %d checks passed\n",checks);return 0;
}
