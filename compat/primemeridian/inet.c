// This file has been generated in whole or in part by a Large Language Model
// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// Linux x86-64 socket ABI. Protocol services use only kernel-owned arguments.
#include <types.h>
#include <inet.h>
#include <proc.h>
#include <usermode.h>
#include <memory.h>
#include <memset.h>
#include <memcpy.h>
#include <errno.h>
#include "linux.h"
#define EAFNOSUPPORT 97
#define ENOPROTOOPT 92
#define EMSGSIZE 90
#define EDOM 33
#define SOCK_NONBLOCK 04000
#define SOCK_CLOEXEC 02000000
#define LIMIT (1024ULL*1024)
typedef struct {u16 family,port;u32 address;u8 zero[8];} address_t;
typedef struct {u64 name;u32 namelen,pad;u64 iov,iovlen,control,controllen;i32 flags;u32 pad2;} message_t;
typedef struct {i64 sec,usec;} timeval_t;
static bool valid(u64 p,u64 n,bool write){return user_range_ok(p,n,write);}
static i64 install(file_t* f,u32 flags){i32 fd=fd_install(proc_current(),f,0);if(fd<0){file_unref(f);return fd;}if(flags&SOCK_CLOEXEC)proc_current()->fd_cloexec|=1u<<fd;return fd;}
static i64 input(u64 p,u64 n,inet_addr_t* out){
 if(n<sizeof(address_t))return -EINVAL;
 if(!valid(p,n,false))return -EFAULT;
 address_t a;memcpy(&a,(void*)p,sizeof a);if(a.family!=2)return -EAFNOSUPPORT;
 *out=(inet_addr_t){.address=a.address,.port=a.port};return 0;
}
/* Validate all output ranges before accept/recv consumes a connection/message.
 * Capacity lives in the kernel here, never masquerades as a user pointer. */
static i64 output_check(u64 p,u32 cap){u32 n=cap<16?cap:16;return n&&!valid(p,n,true)?-EFAULT:0;}
static i64 length_check(u64 p,u64 lp,u32* cap,bool optional){
 if(!p&&optional){*cap=0;return 0;}
 if(!valid(lp,4,true))return -EFAULT;
 memcpy(cap,(void*)lp,4);return output_check(p,*cap);
}
static void output(const inet_addr_t* a,u64 p,u32 cap){address_t out={.family=2,.port=a->port,.address=a->address};u32 n=cap<16?cap:16;if(n)memcpy((void*)p,&out,n);}
static i64 transfer(file_t* f,bool receive,u64 buf,u64 len,u32 flags,u64 addr,u64 alen){
 if(len>LIMIT)return -EMSGSIZE;
 if(len&&!valid(buf,len,receive))return -EFAULT;
 inet_addr_t a={0};u32 cap=0,of=0;i64 r;
 if(receive){r=length_check(addr,alen,&cap,true);if(r<0)return r;}
 else if(addr){r=input(addr,alen,&a);if(r<0)return r;}
 /* Kernel services receive a kernel buffer, even for a direct sendto/recvfrom. */
 u8* data=kmalloc(len+1);if(!data)return -ENOMEM;
 if(!receive&&len)memcpy(data,(void*)buf,len);
 r=receive?inet_recv(f,data,len,flags,&a,&of):inet_send(f,data,len,flags,addr?&a:NULL);
 if(receive&&r>=0){u64 copy=(u64)r<len?(u64)r:len;if(copy)memcpy((void*)buf,data,copy);if(addr){output(&a,addr,cap);*(u32*)alen=16;}}
 kfree(data);return r;
}
static i64 message(file_t* f,bool receive,u64 p,u32 flags){
 if(!valid(p,sizeof(message_t),receive))return -EFAULT;
 message_t m;memcpy(&m,(void*)p,sizeof m);
 if(m.iovlen>1024)return -EINVAL;
 if(m.iovlen&&!valid(m.iov,m.iovlen*sizeof(l_iovec_t),false))return -EFAULT;
 /* Snapshot descriptors so a blocked operation never rereads mutable iovecs. */
 l_iovec_t* iov=kmalloc(m.iovlen*sizeof(l_iovec_t)+1);if(!iov)return -ENOMEM;
 if(m.iovlen)memcpy(iov,(void*)m.iov,m.iovlen*sizeof(l_iovec_t));
 i64 r=0;u64 total=0;inet_addr_t a={0};u32 of=0;
 for(u64 i=0;i<m.iovlen;i++){if(iov[i].iov_len>LIMIT-total){r=-EMSGSIZE;goto done;}total+=iov[i].iov_len;if(iov[i].iov_len&&!valid(iov[i].iov_base,iov[i].iov_len,receive)){r=-EFAULT;goto done;}}
 if(m.controllen){if(!valid(m.control,m.controllen,receive)){r=-EFAULT;goto done;}if(!receive){r=-EINVAL;goto done;}}
 if(m.name){r=receive?output_check(m.name,m.namelen):input(m.name,m.namelen,&a);if(r<0)goto done;}
 u8* data=kmalloc(total+1);if(!data){r=-ENOMEM;goto done;}
 if(!receive){u64 off=0;for(u64 i=0;i<m.iovlen;i++){if(iov[i].iov_len)memcpy(data+off,(void*)iov[i].iov_base,iov[i].iov_len);off+=iov[i].iov_len;}}
 r=receive?inet_recv(f,data,total,flags,&a,&of):inet_send(f,data,total,flags,m.name?&a:NULL);
 if(receive&&r>=0){u64 left=(u64)r<total?(u64)r:total,off=0;for(u64 i=0;i<m.iovlen&&left;i++){u64 n=iov[i].iov_len<left?iov[i].iov_len:left;if(n)memcpy((void*)iov[i].iov_base,data+off,n);off+=n;left-=n;}if(m.name)output(&a,m.name,m.namelen);message_t* out=(message_t*)p;out->namelen=m.name?16:0;out->controllen=0;out->flags=of;}
 kfree(data);
 done:kfree(iov);return r;
}
static u32 option(u64 level,u64 name){
 if(level==6&&name==1)return INET_NODELAY;
 if(level!=1)return 0;
 switch(name){case 2:return INET_REUSEADDR;case 3:return INET_TYPE;case 4:return INET_ERROR;case 7:return INET_SNDBUF;case 8:return INET_RCVBUF;case 9:return INET_KEEPALIVE;case 6:return INET_BROADCAST;case 20:return INET_RCVTIMEO;case 21:return INET_SNDTIMEO;case 30:return INET_ACCEPTCONN;default:return 0;}
}
static i64 options(file_t* f,bool set,u64 level,u64 name,u64 p,u64 len){
 u32 opt=option(level,name);if(!opt)return -ENOPROTOOPT;
 bool timeout=opt==INET_RCVTIMEO||opt==INET_SNDTIMEO;u32 n=timeout?16:4,cap;
 u64 value=0;u32 size=timeout?8:4;i64 r;
 if(set){if(len<n)return -EINVAL;if(!valid(p,n,false))return -EFAULT;
 if(timeout){timeval_t t;memcpy(&t,(void*)p,16);if(t.sec<0||t.usec<0||t.usec>=1000000)return -EDOM;u64 sub=((u64)t.usec+999)/1000;if((u64)t.sec>(~0ULL-sub)/1000)return -ERANGE;value=(u64)t.sec*1000+sub;}else memcpy(&value,(void*)p,4);
 return inet_option(f,true,opt,&value,&size);
 }
 if(!valid(len,4,true))return -EFAULT;
 memcpy(&cap,(void*)len,4);
 u32 copy=cap<n?cap:n;
 if(copy&&!valid(p,copy,true))return -EFAULT;
 r=inet_option(f,false,opt,&value,&size);if(r<0)return r;
 if(timeout){timeval_t t={.sec=(i64)(value/1000),.usec=(i64)(value%1000)*1000};if(copy)memcpy((void*)p,&t,copy);}else if(copy)memcpy((void*)p,&value,copy);
 *(u32*)len=copy;return 0;
}
bool pm_inet_call(u64 nr,u64 a,u64 b,u64 c,u64 d,u64 e,u64 f6,i64* r){
 if(nr==41){if(a==1)return false;if(a!=2){*r=-EAFNOSUPPORT;return true;}if(b&~(u64)(15|SOCK_NONBLOCK|SOCK_CLOEXEC)){*r=-EINVAL;return true;}file_t* f;*r=inet_create(b&15,(b&SOCK_NONBLOCK)?O_NONBLOCK:0,c,&f);if(*r>=0)*r=install(f,b);return true;}
 switch(nr){case 42:case 43:case 44:case 45:case 46:case 47:case 48:case 49:case 50:case 51:case 52:case 54:case 55:case 288:break;default:return false;}
 file_t* f=fd_get(proc_current(),(i32)a);if(!f||!file_is_inet(f))return false;
 switch(nr){
 case 42:case 49:{inet_addr_t addr;*r=input(b,c,&addr);if(*r>=0)*r=nr==42?inet_connect(f,&addr):inet_bind(f,&addr);break;}
 case 43:case 288:{u32 flags=nr==288?(u32)d:0,cap=0;if(d>0xffffffffULL&&nr==288){*r=-EINVAL;break;}if(flags&~(SOCK_NONBLOCK|SOCK_CLOEXEC)){*r=-EINVAL;break;}*r=length_check(b,c,&cap,true);if(*r<0)break;file_t* nf;inet_addr_t peer;*r=inet_accept(f,(flags&SOCK_NONBLOCK)?O_NONBLOCK:0,&nf,&peer);if(*r<0)break;*r=install(nf,flags);if(*r>=0&&b){output(&peer,b,cap);*(u32*)c=16;}break;}
 case 44:case 45:*r=transfer(f,nr==45,b,c,d,e,f6);break;
 case 46:case 47:*r=message(f,nr==47,b,c);break;
 case 48:*r=inet_shutdown(f,(i32)b);break;
 case 50:*r=inet_listen(f,(i32)b);break;
 case 51:case 52:{u32 cap;*r=length_check(b,c,&cap,false);if(*r<0)break;inet_addr_t addr;*r=inet_name(f,&addr,nr==52);if(*r>=0){output(&addr,b,cap);*(u32*)c=16;}break;}
 case 54:case 55:*r=options(f,nr==54,b,c,d,e);break;
 }return true;
}

/* Linux x86-64 ifreq is 40 bytes; its sockaddr union starts at byte 16.
 * Interface numbering is stable even while DHCP has no IPv4 lease. */
typedef struct { char name[16]; u8 data[24]; } interface_t;
typedef struct { i32 length; u32 pad; u64 buffer; } interfaces_t;
_Static_assert(sizeof(interface_t)==40,"Linux ifreq layout");
_Static_assert(sizeof(interfaces_t)==16,"Linux ifconf layout");
static bool interface_name(const char name[16],const char* expected) {
    for(u32 i=0;i<16;i++) {
        if(name[i]!=expected[i])return false;
        if(!expected[i])return true;
    }
    return false;
}
static bool ethernet_present(const net_info_t* n) {
    if(n->mtu)return true;
    for(u32 i=0;i<6;i++)if(n->mac[i])return true;
    return false;
}
static void interface_address(interface_t* out,const char* name,u32 addr) {
    memset(out,0,sizeof(*out));
    for(u32 i=0;name[i];i++)out->name[i]=name[i];
    address_t a={.family=2,.address=addr};
    memcpy(out->data,&a,sizeof a);
}
bool pm_inet_ioctl(file_t* f,u32 req,u64 arg,i64* r) {
    switch(req) {
        case 0x8910: case 0x8912: case 0x8913: case 0x8915:
        case 0x8919: case 0x891b: case 0x8921: case 0x8927:
        case 0x8933: break;
        default:return false;
    }
    if(!file_is_inet(f))return false;
    net_info_t n;net_info(&n);
    char ifname[16];net_ifname(ifname);
    bool eth=ethernet_present(&n);
    *r=-EFAULT;
    if(req==0x8912) {
        if(!valid(arg,sizeof(interfaces_t),true))return true;
        interfaces_t conf;memcpy(&conf,(void*)arg,sizeof conf);
        if(conf.length<0){*r=-EINVAL;return true;}
        interface_t entries[2];
        interface_address(&entries[0],"lo",0x0100007f);
        u32 count=1;
        if(eth&&n.address)interface_address(&entries[count++],ifname,n.address);
        u32 bytes=count*sizeof(interface_t);
        if(conf.buffer) {
            u32 fit=(u32)conf.length/sizeof(interface_t);
            if(fit<count)bytes=fit*sizeof(interface_t);
            if(bytes&&!valid(conf.buffer,bytes,true))return true;
            if(bytes)memcpy((void*)conf.buffer,entries,bytes);
        }
        *(i32*)arg=(i32)bytes;
        *r=0;return true;
    }
    if(!valid(arg,sizeof(interface_t),true))return true;
    interface_t out;memcpy(&out,(void*)arg,sizeof out);
    bool loop;
    if(req==0x8910) {
        i32 index;memcpy(&index,out.data,4);
        if(index!=1&&!(index==2&&eth)){*r=-ENODEV;return true;}
        loop=index==1;
        memset(out.name,0,sizeof out.name);
        const char* name=loop?"lo":ifname;
        for(u32 i=0;name[i];i++)out.name[i]=name[i];
    } else {
        loop=interface_name(out.name,"lo");
        if(!loop&&!(eth&&interface_name(out.name,ifname))){*r=-ENODEV;return true;}
        memset(out.data,0,sizeof out.data);
        if(req==0x8913) {
            u16 flags=loop?(1|8|64):(2|4096|(n.up?1:0)|(n.link?64:0));
            memcpy(out.data,&flags,2);
        } else if(req==0x8921||req==0x8933) {
            i32 value=req==0x8933?(loop?1:2):(loop?65536:(i32)n.mtu);
            memcpy(out.data,&value,4);
        } else if(req==0x8927) {
            u16 hardware=loop?772:1;
            memcpy(out.data,&hardware,2);
            if(!loop)memcpy(out.data+2,n.mac,6);
        } else {
            u32 addr;
            if(req==0x8915)addr=loop?0x0100007f:n.address;
            else if(req==0x891b)addr=loop?0x000000ff:n.netmask;
            else addr=loop?0:n.address|~n.netmask;
            address_t a={.family=2,.address=addr};
            memcpy(out.data,&a,sizeof a);
        }
    }
    memcpy((void*)arg,&out,sizeof out);
    *r=0;return true;
}
