// This file has been generated in whole or in part by a Large Language Model
#include <types.h>
#include <wifi_control.h>
#include <inet.h>
#include <file.h>
#include <kprintf.h>
#include <ktime.h>
#include <klog.h>
// PID 0 only: a DHCP change updates the resolver through normal filesystem I/O.
void net_config_poll(void){
 wifi_control_poll();
 static u32 previous;static u64 retry;u32 dns=net_dns_address();
 if(dns==previous||time_counter_ns()<retry)return;
 retry=time_counter_ns()+5000000000ULL;
 u8* b=(u8*)&dns;char text[80];i32 length=dns?ksnprintf(text,sizeof text,"nameserver %u.%u.%u.%u\n",b[0],b[1],b[2],b[3]):0;
 file_t* file;if(file_open_mode("/etc/resolv.conf",O_WRONLY|O_CREAT|O_TRUNC,0644,&file)<0)return;
 if(file_write(file,text,length)==length){previous=dns;klog(LOG_OKAY,"net: DNS resolver %u.%u.%u.%u\n",b[0],b[1],b[2],b[3]);}
 file_unref(file);
}
