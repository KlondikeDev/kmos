// This file has been generated in whole or in part by a Large Language Model
#include "kunix.h"
#include "../include/net_abi.h"
static void address(const char* name,unsigned int a){unsigned char* b=(unsigned char*)&a;printf("%s %u.%u.%u.%u\n",name,b[0],b[1],b[2],b[3]);}
static void interface_name(char out[16]){strcpy(out,"network");int fd=open("/proc/netinfo",O_RDONLY);if(fd<0)return;char b[16];ssize_t n=read(fd,b,sizeof(b));close(fd);for(ssize_t i=0;i<n && i<15;i++)if(b[i]==':'){memcpy(out,b,i);out[i]=0;return;}}
int main(void){char name[16];interface_name(name);struct knet_info info;long r=syscall3(SYS_NETINFO,(long)&info,0,0);if(r<0){printf("netinfo: error %ld\n",r);return 1;}printf("lo: 127.0.0.1/8\n%s: %s, link %s, mtu %u\n",name,info.up?"up":"down",info.link?"up":"down",info.mtu);printf("MAC %02x:%02x:%02x:%02x:%02x:%02x\n",info.mac[0],info.mac[1],info.mac[2],info.mac[3],info.mac[4],info.mac[5]);address("address",info.address);address("netmask",info.netmask);address("gateway",info.gateway);address("DNS",info.dns);printf("packets rx=%lu tx=%lu drops rx=%lu tx=%lu\n",(u64)info.rx_packets,(u64)info.tx_packets,(u64)info.rx_dropped,(u64)info.tx_dropped);return 0;}
