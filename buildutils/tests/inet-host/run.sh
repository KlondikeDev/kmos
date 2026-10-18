#!/bin/sh
# This file has been generated in whole or in part by a Large Language Model
set -eu
# LSan cannot inspect threads under the tool runner ptrace sandbox.
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
cd "$(dirname "$0")/../../.."
sources=""
for name in init def inet_chksum ip mem memp netif pbuf stats sys tcp tcp_in tcp_out timeouts udp dns; do
 sources="$sources vendor/lwip/src/core/$name.c"
done
for name in acd dhcp etharp icmp ip4 ip4_addr ip4_frag; do
 sources="$sources vendor/lwip/src/core/ipv4/$name.c"
done
cc -std=gnu11 -g -O1 -fno-builtin ${INET_SANITIZE:--fsanitize=address,undefined} -Ibuildutils/tests/inet-host -Iinclude/lwip-port -Ivendor/lwip/src/include -Iinclude -Iinclude/klibc kernel/inet.c buildutils/tests/inet-host/test.c $sources vendor/lwip/src/netif/ethernet.c -o /tmp/kunix-inet-host
/tmp/kunix-inet-host

/tmp/kunix-inet-host wired
