// This file has been generated in whole or in part by a Large Language Model
#pragma once
#include <types.h>
#include <file.h>
// Shared kernel services, independent of syscall personality. Network byte order.
typedef struct { u32 address; u16 port; u16 reserved; } inet_addr_t;
typedef struct {
 u32 address, netmask, gateway, dns;
 u32 mtu, up, link;
 u8 mac[6]; u8 reserved[6];
 u64 rx_packets, tx_packets, rx_dropped, tx_dropped;
} net_info_t;
enum { INET_REUSEADDR=1, INET_NODELAY, INET_KEEPALIVE, INET_RCVTIMEO,
       INET_SNDTIMEO, INET_ERROR, INET_TYPE, INET_ACCEPTCONN, INET_SNDBUF,
       INET_RCVBUF, INET_BROADCAST };
void net_init(void);
void net_poll(void);
void net_info(net_info_t* out);
// Active interface name ("eth0", "wlan0") for interface queries.
void net_ifname(char out[16]);
// Copy the current DHCP DNS address (network order), or zero when unavailable.
u32 net_dns_address(void);
bool file_is_inet(const file_t* f);
i32 inet_create(u32 type,u32 flags,u32 protocol,file_t** out);
i32 inet_bind(file_t* f,const inet_addr_t* addr);
i32 inet_connect(file_t* f,const inet_addr_t* addr);
i32 inet_listen(file_t* f,i32 backlog);
i32 inet_accept(file_t* f,u32 flags,file_t** out,inet_addr_t* peer);
i32 inet_shutdown(file_t* f,i32 how);
i32 inet_name(file_t* f,inet_addr_t* out,bool peer);
i64 inet_send(file_t* f,const void* buf,u64 len,u32 flags,const inet_addr_t* to);
i64 inet_recv(file_t* f,void* buf,u64 len,u32 flags,inet_addr_t* from,u32* out_flags);
// Scalars are i32; timeouts are u64 milliseconds. ERROR is read-and-clear.
i32 inet_option(file_t* f,bool set,u32 option,void* value,u32* length);
