// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#pragma once

#include <types.h>
#include <file.h>

// Local (Unix domain) sockets: stream, datagram and sequenced-packet.
//
// A socket is an open file. It can be named in the filesystem (bind
// creates a socket node there) or in the abstract namespace (a name
// starting with NUL, Linux's extension, which never touches a disk).
// Connected stream sockets are a pair of byte pipes; datagram and
// seqpacket ones carry whole messages. Open files can travel with the
// data (SCM_RIGHTS), and each side can ask who the other is (SO_PEERCRED).
//
// Values (types, flags, errors) are Linux's; Prime Meridian passes them
// through.

#define SK_STREAM    1
#define SK_DGRAM     2
#define SK_SEQPACKET 5

#define SK_MSG_PEEK     0x2
#define SK_MSG_TRUNC    0x20
#define SK_MSG_DONTWAIT 0x40
#define SK_MSG_WAITALL  0x100
#define SK_MSG_NOSIGNAL 0x4000
#define SK_MSG_CMSG_CLOEXEC 0x40000000

#define SK_PATH_MAX 108              // sun_path

// A name: abstract (the bytes after the leading NUL) or a path; len 0 is
// "unnamed".
typedef struct {
    bool abstract;
    u32 len;
    char name[SK_PATH_MAX];
} sk_addr_t;

#define SK_MAX_FDS 16                // files in one SCM_RIGHTS message

typedef struct {
    i32 pid;
    u32 uid, gid;
} sk_cred_t;

i32 sock_create(u32 type, u32 flags, file_t** out);
i32 sock_pair(u32 type, u32 flags, file_t** a, file_t** b);
bool file_is_socket(const file_t* f);
u32 sock_type(const file_t* f);

i32 sock_bind(file_t* f, const sk_addr_t* addr);
i32 sock_listen(file_t* f, i32 backlog);
i32 sock_connect(file_t* f, const sk_addr_t* addr);
// A connection, as a new open file; peer is the connecting side's name.
i32 sock_accept(file_t* f, u32 flags, file_t** out, sk_addr_t* peer);
i32 sock_shutdown(file_t* f, i32 how);

// Send len bytes (gathered by the caller into one buffer), with files to
// pass along (nfiles may be 0) and, for unconnected datagram sockets, a
// destination. Returns bytes sent or -errno.
i64 sock_send(file_t* f, const void* buf, u64 len, u32 flags, const sk_addr_t* to,
              file_t** files, u32 nfiles);
// Receive up to len bytes. *from gets the sender's name (datagrams); files
// that came with the data are handed back in files/nfiles (their
// references now belong to the caller); *out_flags gets MSG_TRUNC when a
// message did not fit.
i64 sock_recv(file_t* f, void* buf, u64 len, u32 flags, sk_addr_t* from,
              file_t** files, u32* nfiles, u32* out_flags);

i32 sock_name(file_t* f, sk_addr_t* out);         // getsockname
i32 sock_peer_name(file_t* f, sk_addr_t* out);    // getpeername
i32 sock_peer_cred(file_t* f, sk_cred_t* out);    // SO_PEERCRED
bool sock_listening(file_t* f);
