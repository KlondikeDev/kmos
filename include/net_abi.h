// This file has been generated in whole or in part by a Large Language Model
#pragma once
// Native Kunix networking ABI. Addresses/ports are in network byte order.
#define KNET_IPV4 1
#define KNET_STREAM 1
#define KNET_DGRAM 2
#define KNET_NONBLOCK 1
#define KNET_CLOEXEC 2
#define KNET_PEEK 1
#define KNET_DONTWAIT 2
#define KNET_WAITALL 4
#define KNET_NOSIGNAL 8
#define KNET_TRUNC 16
#define KNET_REUSEADDR 1
#define KNET_NODELAY 2
#define KNET_KEEPALIVE 3
#define KNET_RCVTIMEO 4
#define KNET_SNDTIMEO 5
#define KNET_ERROR 6
#define KNET_TYPE 7
#define KNET_ACCEPTCONN 8
#define KNET_SNDBUF 9
#define KNET_RCVBUF 10
#define KNET_BROADCAST 11
struct knet_addr { unsigned int address; unsigned short port, reserved; };
struct knet_info {
 unsigned int address, netmask, gateway, dns, mtu, up, link;
 unsigned char mac[6], reserved[6];
 unsigned long long rx_packets, tx_packets, rx_dropped, tx_dropped;
};
