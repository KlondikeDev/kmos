// This file has been generated in whole or in part by a Large Language Model
#include <inet.h>
#include <memory.h>
#include <proc.h>
#include <signal.h>
#include <errno.h>
#include <io.h>
#include <pit.h>
#include <ktime.h>
#include <e1000.h>
#include <random.h>
#include <string.h>
#include <lwip/init.h>
#include <lwip/netif.h>
#include <lwip/timeouts.h>
#include <lwip/tcp.h>
#include <lwip/udp.h>
#include <lwip/raw.h>
#include <lwip/dhcp.h>
#include <lwip/dns.h>
#include <lwip/etharp.h>
#include <netif/ethernet.h>
#include <iwx200.h>

#define N_STREAM 1
#define N_DGRAM 2
#define N_RAW 3
#define N_PEEK 2
#define N_TRUNC 32
#define N_DONTWAIT 64
#define N_WAITALL 256
#define N_NOSIGNAL 16384
#define RX_LIMIT 65536U
#define QUEUE_LIMIT 128U
#define BACKLOG_LIMIT 32U
#define EADDRINUSE 98
#define ENOTCONN 107
#define EISCONN 106
#define ECONNRESET 104
#define ECONNREFUSED 111
#define EINPROGRESS 115
#define EALREADY 114
#define EOPNOTSUPP 95
#define EMSGSIZE 90
#define EDESTADDRREQ 89

typedef struct packet { struct packet* next;
    struct pbuf* p;
    inet_addr_t from;
    u32 off;
} packet_t;
typedef struct endpoint {
    u32 type;
    struct tcp_pcb* tcp;
    struct udp_pcb* udp;
    struct raw_pcb* raw;
    inet_addr_t local_name,peer_name;
    bool listening, connecting, connected, eof, rd, wr;
    i32 error;
    u32 rlimit, slimit;
    u64 rtime, stime;
    packet_t *head,*tail;
    u32 bytes, packets;
    struct endpoint* pending[BACKLOG_LIMIT];
    u32 count, backlog;
} endpoint_t;
static struct netif eth;
static struct netif wlan;
static bool initialized, nic, wifi, wifi_dhcp_started;
static net_info_t counters;
static const file_ops_t inet_ops;
static endpoint_t* ep(const file_t*f) { return f->priv;
}
bool file_is_inet(const file_t*f) { return f && f->ops==&inet_ops;
}
static i32 error_code(err_t e) {
    switch(e) { case ERR_OK:return 0;
        case ERR_MEM:case ERR_BUF:return ENOMEM;
        case ERR_TIMEOUT:return 110;
        case ERR_RTE:return 101;
        case ERR_USE:return EADDRINUSE;
        case ERR_ISCONN:return EISCONN;
        case ERR_CONN:return ENOTCONN;
        case ERR_ABRT:return 103;
        case ERR_RST:return ECONNRESET;
        case ERR_CLSD:return EPIPE;
        case ERR_ARG:case ERR_VAL:return EINVAL;
        default:return EIO;
    }
}
static void wake(endpoint_t*s) { sched_wake_obj(s);
    sched_wake_obj(&file_poll_wait);
}
u32_t sys_now(void) { return (u32)(time_counter_ns()/1000000ULL);
}
u32 kunix_lwip_rand(void) {
    u32 word;
    if(random_read(&word,sizeof(word))==sizeof(word))return word;
    /* Transport must still function on CPUs without a hardware RNG. This
     * fallback provides protocol variation, never authentication entropy. */
    static u32 state=0x19a61f4b;
    state^=ticks;
    state^=state<<13;
    state^=state>>17;
    state^=state<<5;
    return state;
}
static err_t eth_send(struct netif*n,struct pbuf*p) {
    (void)n;
    u8 frame[1518];
    if(p->tot_len>sizeof(frame)){counters.tx_dropped++;
        return ERR_BUF;
    }
    pbuf_copy_partial(p,frame,p->tot_len,0);
    if(!e1000_send(frame,p->tot_len)){counters.tx_dropped++;
        return ERR_IF;
    }
    counters.tx_packets++;
    return ERR_OK;
}
static err_t eth_init(struct netif*n) {
    n->name[0]='e';
    n->name[1]='n';
    n->hostname="kunix";
    n->mtu=1500;
    n->hwaddr_len=6;
    e1000_mac(n->hwaddr);
    n->flags=NETIF_FLAG_BROADCAST|NETIF_FLAG_ETHARP|NETIF_FLAG_ETHERNET;
    n->output=etharp_output;
    n->linkoutput=eth_send;
    return ERR_OK;
}
static err_t wifi_send(struct netif*n,struct pbuf*p) {
    (void)n;
    u8 frame[1518];
    if(p->tot_len>sizeof(frame)){counters.tx_dropped++;
        return ERR_BUF;
    }
    pbuf_copy_partial(p,frame,p->tot_len,0);
    if(!iwx200_send(frame,p->tot_len)){counters.tx_dropped++;
        return ERR_IF;
    }
    counters.tx_packets++;
    return ERR_OK;
}
static err_t wifi_init(struct netif*n) {
    n->name[0]='w';
    n->name[1]='l';
    n->hostname="kunix";
    n->mtu=1500;
    n->hwaddr_len=6;
    iwx200_mac(n->hwaddr);
    n->flags=NETIF_FLAG_BROADCAST|NETIF_FLAG_ETHARP|NETIF_FLAG_ETHERNET;
    n->output=etharp_output;
    n->linkoutput=wifi_send;
    return ERR_OK;
}
void net_init(void) {
    u64 q=irq_save();
    if(initialized){irq_restore(q);
        return;
    }lwip_init();
    initialized=true;
    nic=e1000_init();
    // Firmware is loaded only after the root filesystem is mounted.
    // net_poll registers wireless when that later initialization succeeds.
    if(nic){ip4_addr_t z={0};        netif_add(&eth,&z,&z,&z,0,eth_init,ethernet_input);
        netif_set_default(&eth);
        netif_set_up(&eth);
        if(e1000_link())netif_set_link_up(&eth);
        dhcp_start(&eth);
    }
    irq_restore(q);
}
void net_poll(void) {
    if(!initialized)return;
    u64 q=irq_save();
    if(!wifi && iwx200_online()){
        // Wireless link: the interface exists once the card is alive;
        // DHCP starts after association (see net_poll).
        ip4_addr_t z={0};
        if(netif_add(&wlan,&z,&z,&z,0,wifi_init,ethernet_input)){
        netif_set_up(&wlan);
        wifi=true;
        }
    }
    if(nic){if(e1000_link())netif_set_link_up(&eth);
        else netif_set_link_down(&eth);
        for(u32 i=0;i<32;i++){u8 frame[1518];
            u32 n=e1000_receive(frame,sizeof(frame));
            if(!n)break;
            struct pbuf*p=pbuf_alloc(PBUF_RAW,(u16)n,PBUF_POOL);
            if(!p){counters.rx_dropped++;
                continue;
            }
            pbuf_take(p,frame,n);
            counters.rx_packets++;
            if(eth.input(p,&eth)!=ERR_OK){pbuf_free(p);
                counters.rx_dropped++;
            }}
    }
    if(wifi){
        if(iwx200_link()==IWX_LINK_UP){
            if(!netif_is_link_up(&wlan)){
                netif_set_link_up(&wlan);
                if(!netif_default || !netif_is_link_up(netif_default)) {
                    netif_set_default(&wlan);
                    ip_addr_t z={0};for(u32 i=0;i<DNS_MAX_SERVERS;i++)dns_setserver(i,&z);
                }
            }
            if(!wifi_dhcp_started)wifi_dhcp_started=dhcp_start(&wlan)==ERR_OK;
        } else if(netif_is_link_up(&wlan)) {
            netif_set_link_down(&wlan);
            dhcp_stop(&wlan);wifi_dhcp_started=false;
            if(netif_default==&wlan){ip_addr_t z={0};for(u32 i=0;i<DNS_MAX_SERVERS;i++)dns_setserver(i,&z);}
            ip4_addr_t z={0};netif_set_addr(&wlan,&z,&z,&z);
            if(nic)netif_set_default(&eth);
        }
        for(u32 i=0;i<32;i++){u8 frame[1518];
            u32 n=iwx200_receive(frame,sizeof(frame));
            if(!n)break;
            struct pbuf*p=pbuf_alloc(PBUF_RAW,(u16)n,PBUF_POOL);
            if(!p){counters.rx_dropped++;
                continue;
            }
            pbuf_take(p,frame,n);
            counters.rx_packets++;
            if(wlan.input(p,&wlan)!=ERR_OK){pbuf_free(p);
                counters.rx_dropped++;
            }}
    }
    // Carrier changes can happen while the other link stays up.
    if(nic && netif_is_link_up(&eth))netif_set_default(&eth);
    else if(wifi && netif_is_link_up(&wlan))netif_set_default(&wlan);
    netif_poll_all();
    sys_check_timeouts();
    irq_restore(q);
}
u32 net_dns_address(void) { u64 q=irq_save();
    u32 a=initialized?ip4_addr_get_u32(ip_2_ip4(dns_getserver(0))):0;
    irq_restore(q);
    return a;
}
void net_info(net_info_t*out) {    u64 q=irq_save();
    *out=counters;
    out->dns=net_dns_address();
    struct netif* active=wifi && (!nic || netif_default==&wlan)?&wlan:nic?&eth:0;
    if(active){out->address=ip4_addr_get_u32(netif_ip4_addr(active));
        out->netmask=ip4_addr_get_u32(netif_ip4_netmask(active));
        out->gateway=ip4_addr_get_u32(netif_ip4_gw(active));
        out->up=netif_is_up(active);
        out->link=netif_is_link_up(active);
        out->mtu=active->mtu;
        memcpy(out->mac,active->hwaddr,6);
    }irq_restore(q);
}
void net_ifname(char out[16]) {
    u64 q=irq_save();
    const char* name=wifi && (!nic || netif_default==&wlan)?"wlan0":"eth0";
    for(u32 i=0;i<16;i++){out[i]=name[i];if(!name[i])break;}
    irq_restore(q);
}
static void clear_packets(endpoint_t*s) {while(s->head){packet_t*p=s->head;
        s->head=p->next;
        pbuf_free(p->p);
        kfree(p);
    }s->tail=0;
    s->bytes=s->packets=0;
}
static bool enqueue(endpoint_t*s,struct pbuf*p,const inet_addr_t*from) {
    if(s->packets>=QUEUE_LIMIT || p->tot_len>s->rlimit-s->bytes)return false;
    packet_t*m=kmalloc(sizeof(*m));
    if(!m)return false;
    memset(m,0,sizeof(*m));
    m->p=p;
    if(from)m->from=*from;
    if(s->tail)s->tail->next=m;
    else s->head=m;
    s->tail=m;
    s->bytes+=p->tot_len;
    s->packets++;
    wake(s);
    return true;
}
static err_t tcp_receive(void*arg,struct tcp_pcb*p,struct pbuf*b,err_t err) {
    endpoint_t*s=arg;
    (void)err;
    if(!b){s->eof=true;
        wake(s);
        return ERR_OK;
    }
    if(s->rd){tcp_recved(p,b->tot_len);
        pbuf_free(b);
        return ERR_OK;
    }
    return enqueue(s,b,0)?ERR_OK:ERR_MEM;
}
static void tcp_error(void*arg,err_t err) {endpoint_t*s=arg;
    s->tcp=0;
    s->error=err==ERR_CLSD?0:(s->connecting&&err==ERR_RST)?ECONNREFUSED:error_code(err);
    s->connecting=false;
    s->eof=true;
    wake(s);
}
static err_t tcp_sent_cb(void*arg,struct tcp_pcb*p,u16_t n) {(void)p;
    (void)n;
    wake(arg);
    return ERR_OK;
}
static err_t tcp_connected_cb(void*arg,struct tcp_pcb*p,err_t e) {(void)p;
    endpoint_t*s=arg;
    s->connecting=false;
    s->connected=e==ERR_OK;
    if(e==ERR_OK){s->local_name.address=ip4_addr_get_u32(ip_2_ip4(&p->local_ip));s->local_name.port=lwip_htons(p->local_port);s->peer_name.address=ip4_addr_get_u32(ip_2_ip4(&p->remote_ip));s->peer_name.port=lwip_htons(p->remote_port);}
    s->error=error_code(e);
    wake(s);
    return ERR_OK;
}
/* lwIP silently reclaims TIME_WAIT PCBs. The ext destructor also covers that
* path, so a file never retains a freed PCB after a successful half-close. */
static void cache_names(endpoint_t*s,struct tcp_pcb*p) {
    s->local_name.address=ip4_addr_get_u32(ip_2_ip4(&p->local_ip));
    s->local_name.port=lwip_htons(p->local_port);
    s->peer_name.address=ip4_addr_get_u32(ip_2_ip4(&p->remote_ip));
    s->peer_name.port=lwip_htons(p->remote_port);
}
static void pcb_destroyed(u8_t id,void*arg) {(void)id;
    endpoint_t*s=arg;
    if(s){s->tcp=0;
        s->eof=true;
        wake(s);
    }}
static const struct tcp_ext_arg_callbacks pcb_callbacks={pcb_destroyed,0};
static void attach(endpoint_t*s,struct tcp_pcb*p) {s->tcp=p;
    tcp_ext_arg_set(p,0,s);
    tcp_ext_arg_set_callbacks(p,0,&pcb_callbacks);
    tcp_arg(p,s);
    tcp_recv(p,tcp_receive);
    tcp_err(p,tcp_error);
    tcp_sent(p,tcp_sent_cb);
}
static endpoint_t* alloc_ep(u32 type) {endpoint_t*s=kmalloc(sizeof(*s));
    if(s){memset(s,0,sizeof(*s));
        s->type=type;
        s->rlimit=RX_LIMIT;
        s->slimit=TCP_SND_BUF;
        s->backlog=BACKLOG_LIMIT;
    }return s;
}
static void destroy(endpoint_t*s) {
    for(u32 i=0;i<s->count;i++)destroy(s->pending[i]);
    if(s->tcp){struct tcp_pcb*p=s->tcp;
        tcp_ext_arg_set(p,0,0);
        tcp_arg(p,0);
        if(s->listening){tcp_accept(p,0);
        }else{tcp_recv(p,0);
            tcp_sent(p,0);
            tcp_err(p,0);
        }
        /* close deliberately resets connections with unread bytes, per lwIP contract. */
        if(tcp_close(p)!=ERR_OK)tcp_abort(p);
        s->tcp=0;
    }
    if(s->udp)udp_remove(s->udp);
    if(s->raw)raw_remove(s->raw);
    clear_packets(s);
    kfree(s);
}
static err_t accept_cb(void*arg,struct tcp_pcb*p,err_t e) {
    endpoint_t*l=arg;
    if(e!=ERR_OK || l->count>=l->backlog)return ERR_MEM;
    endpoint_t*s=alloc_ep(N_STREAM);
    if(!s)return ERR_MEM;
    attach(s,p);
    s->rtime=l->rtime;s->stime=l->stime;s->rlimit=l->rlimit;s->slimit=l->slimit;
    cache_names(s,p);
    s->connected=true;
    l->pending[l->count++]=s;
    tcp_backlog_delayed(p);
    wake(l);
    return ERR_OK;
}
static void udp_receive(void*arg,struct udp_pcb*p,struct pbuf*b,const ip_addr_t*a,u16_t port) {
    (void)p;
    endpoint_t*s=arg;
    inet_addr_t from={ip4_addr_get_u32(ip_2_ip4(a)),lwip_htons(port),0};
    if(s->rd||!enqueue(s,b,&from))pbuf_free(b);
}
/* The raw callback sees the IPv4 header. Keep a separate copy: lwIP
 * subsequently removes headers and changes echo requests into replies. A
 * socket must not consume the packet before the normal ICMP responder. */
static u8_t icmp_receive(void*arg,struct raw_pcb*pcb,struct pbuf*p,const ip_addr_t*a) {
    (void)pcb;
    endpoint_t*s=arg;
    if(s->rd||s->packets>=QUEUE_LIMIT||p->tot_len>s->rlimit-s->bytes)return 0;
    struct pbuf*copy=pbuf_clone(PBUF_RAW,PBUF_RAM,p);
    if(copy){inet_addr_t from={ip4_addr_get_u32(ip_2_ip4(a)),0,0};
        if(!enqueue(s,copy,&from))pbuf_free(copy);
    }
    return 0;
}
static file_t* make_file(endpoint_t*s,u32 flags) {file_t*f=kmalloc(sizeof(*f));
    if(f){memset(f,0,sizeof(*f));
        f->priv=s;
        f->refs=1;
        f->flags=O_RDWR|(flags&O_NONBLOCK);
        f->ops=&inet_ops;
    }return f;
}
i32 inet_create(u32 type,u32 flags,u32 protocol,file_t**out) {
    if(type!=N_STREAM&&type!=N_DGRAM&&type!=N_RAW)return -94;
    if(type==N_RAW){
        if(protocol!=1)return -93;
        process_t*p=proc_current();
        if(p&&p->euid)return -EPERM;
    }else if(protocol && protocol!=(type==N_STREAM?6:17))return -93;
    if(flags&~(O_NONBLOCK|O_CLOEXEC))return -EINVAL;
    u64 q=irq_save();
    endpoint_t*s=alloc_ep(type);
    if(!s){irq_restore(q);
        return -ENOMEM;
    }
    if(type==1){struct tcp_pcb*p=tcp_new();
        if(p)attach(s,p);
    }else if(type==N_RAW){s->raw=raw_new(1);
        if(s->raw)raw_recv(s->raw,icmp_receive,s);
    }else{s->udp=udp_new();
        if(s->udp)udp_recv(s->udp,udp_receive,s);
    }
    file_t*f=(s->tcp||s->udp||s->raw)?make_file(s,flags):0;
    if(!f){destroy(s);
        irq_restore(q);
        return -ENOMEM;
    }
    *out=f;
    irq_restore(q);
    return 0;
}
static ip_addr_t ip_address(u32 a) {ip_addr_t ip;
    ip_addr_set_ip4_u32(&ip,a);
    return ip;
}
i32 inet_bind(file_t*f,const inet_addr_t*a) {
    u64 q=irq_save();
    endpoint_t*s=ep(f);
    u16 port=lwip_ntohs(a->port);
    process_t*p=proc_current();
    if(s->type!=N_RAW&&port&&port<1024&&p&&p->euid){irq_restore(q);
        return -EACCES;
    }
    if(s->listening||s->connected||s->connecting){irq_restore(q);
        return -EINVAL;
    }
    ip_addr_t ip=ip_address(a->address);
    err_t e=s->raw?raw_bind(s->raw,&ip):s->type==1?tcp_bind(s->tcp,&ip,port):udp_bind(s->udp,&ip,port);
    if(e==ERR_OK&&s->tcp)cache_names(s,s->tcp);
    irq_restore(q);
    return -error_code(e);
}
/* Socket waits occur outside IRQ exclusion and outside every lwIP callback. */
static i32 wait_for(file_t*f,endpoint_t*s,u32 flags,u64 timeout,u64 start) {
    if((f->flags&O_NONBLOCK)||(flags&N_DONTWAIT))return -EAGAIN;
    if(signal_pending(proc_current()))return -EINTR;
    if(timeout){
        u64 elapsed=time_counter_ns()/1000000ULL-start;
        if(elapsed>=timeout)return -EAGAIN;
        u64 remaining=timeout-elapsed;
        u64 delta=remaining/4+(remaining%4!=0);
        /* Scheduler deadlines are PIT ticks. Recheck the TSC clock at least
         * every 20 ms of PIT progress, including after disk I/O lost ticks. */
        if(delta>5)delta=5;
        sched_block_on_until(s,ticks+(u32)delta);
    }else sched_block_on(s);
    if(signal_pending(proc_current()))return -EINTR;
    if(timeout&&time_counter_ns()/1000000ULL-start>=timeout)return -EAGAIN;
    return 0;
}
i32 inet_connect(file_t*f,const inet_addr_t*a) {
    endpoint_t*s=ep(f);
    u64 q=irq_save();
    if(s->listening){irq_restore(q);
        return -EOPNOTSUPP;
    }
    if(s->connecting){irq_restore(q);
        return -EALREADY;
    }if(s->connected&&s->type==1){irq_restore(q);
        return -EISCONN;
    }
    ip_addr_t ip=ip_address(a->address);
    err_t e;
    if(s->raw){e=raw_connect(s->raw,&ip);
        s->connected=e==ERR_OK;
        irq_restore(q);
        return -error_code(e);
    }
    if(s->type==2){e=udp_connect(s->udp,&ip,lwip_ntohs(a->port));
        s->connected=e==ERR_OK;

        irq_restore(q);
        return -error_code(e);
    }
    if(!s->tcp){irq_restore(q);
        return -ENOTCONN;
    }s->connecting=true;
    s->error=0;
    e=tcp_connect(s->tcp,&ip,lwip_ntohs(a->port),tcp_connected_cb);
    if(e!=ERR_OK){s->connecting=false;
        irq_restore(q);
        return -error_code(e);
    }
    irq_restore(q);
    if(f->flags&O_NONBLOCK)return -EINPROGRESS;
    u64 start=time_counter_ns()/1000000ULL;
    for(;;){q=irq_save();
        bool pending=s->connecting;
        i32 error=s->error;
        irq_restore(q);
        if(!pending)return -error;
        i32 r=wait_for(f,s,0,s->stime,start);
        if(r)return r;
    }
}
i32 inet_listen(file_t*f,i32 backlog) {
    endpoint_t*s=ep(f);
    if(s->type!=1)return -EOPNOTSUPP;
    u64 q=irq_save();
    if(s->connected||s->connecting||!s->tcp){irq_restore(q);
        return -EINVAL;
    }
    u32 n=backlog<1?1:(u32)backlog;
    if(n>BACKLOG_LIMIT)n=BACKLOG_LIMIT;
    if(!s->listening){err_t e;
        if(!s->tcp->local_port){e=tcp_bind(s->tcp,IP_ADDR_ANY,0);if(e!=ERR_OK){irq_restore(q);return -error_code(e);}}
        struct tcp_pcb*p=tcp_listen_with_backlog_and_err(s->tcp,(u8)n,&e);
        if(!p){irq_restore(q);
            return -error_code(e);
        }s->tcp=p;
        s->eof=false;
        s->listening=true;
        tcp_arg(p,s);
        tcp_accept(p,accept_cb);
    }
    s->backlog=n;
    tcp_backlog_set(s->tcp,(u8)n);
    irq_restore(q);
    return 0;
}
i32 inet_accept(file_t*f,u32 flags,file_t**out,inet_addr_t*peer) {
    endpoint_t*l=ep(f);
    if(!l->listening)return -EINVAL;
    u64 start=time_counter_ns()/1000000ULL;
    for(;;){u64 q=irq_save();
        if(l->count){endpoint_t*s=l->pending[0];
            file_t*n=make_file(s,flags);
            if(!n){irq_restore(q);
                return -ENOMEM;
            }for(u32 i=1;i<l->count;i++)l->pending[i-1]=l->pending[i];
            l->count--;
            if(s->tcp)tcp_backlog_accepted(s->tcp);
            *out=n;
            if(peer)inet_name(n,peer,true);
            irq_restore(q);
            return 0;
        }
        irq_restore(q);
        i32 r=wait_for(f,l,0,l->rtime,start);
        if(r)return r;
    }
}
i32 inet_name(file_t*f,inet_addr_t*out,bool peer) {
    endpoint_t*s=ep(f);
    u64 q=irq_save();
    memset(out,0,sizeof(*out));
    if(peer&&!s->connected){irq_restore(q);
        return -ENOTCONN;
    }
    if(s->type==1){if(!s->tcp){*out=peer?s->peer_name:s->local_name;irq_restore(q);return 0;}
        cache_names(s,s->tcp);
        out->address=ip4_addr_get_u32(ip_2_ip4(peer?&s->tcp->remote_ip:&s->tcp->local_ip));
        out->port=lwip_htons(peer?s->tcp->remote_port:s->tcp->local_port);
    }else if(s->raw){out->address=ip4_addr_get_u32(ip_2_ip4(peer?&s->raw->remote_ip:&s->raw->local_ip));
    }else{out->address=ip4_addr_get_u32(ip_2_ip4(peer?&s->udp->remote_ip:&s->udp->local_ip));
        out->port=lwip_htons(peer?s->udp->remote_port:s->udp->local_port);
    }
    irq_restore(q);
    return 0;
}
i32 inet_shutdown(file_t*f,i32 how) {
    if(how<0||how>2)return -EINVAL;
    endpoint_t*s=ep(f);
    u64 q=irq_save();
    if(!s->connected){irq_restore(q);
        return -ENOTCONN;
    }
    /* Keep lwIP receiving and acknowledging after SHUT_RD. Setting its
    TF_RXCLOSED would suppress the callback that invalidates our PCB on FIN. */
    err_t e=ERR_OK;
    if(s->tcp&&how!=0&&!s->wr)e=tcp_shutdown(s->tcp,0,1);
    if(e==ERR_OK){if(how!=1){s->rd=true;
            if(s->tcp&&s->bytes)tcp_recved(s->tcp,(u16)s->bytes);
            clear_packets(s);
        }if(how!=0)s->wr=true;
        wake(s);
    }irq_restore(q);
    return -error_code(e);
}
static i32 pipe_error(u32 flags) {if(!(flags&N_NOSIGNAL))signal_send(proc_current(),SIGPIPE);
    return -EPIPE;
}
i64 inet_send(file_t*f,const void*buf,u64 len,u32 flags,const inet_addr_t*to) {
    if(flags&~(N_DONTWAIT|N_NOSIGNAL))return -EOPNOTSUPP;
    endpoint_t*s=ep(f);
    u64 start=time_counter_ns()/1000000ULL;
    for(;;){u64 q=irq_save();
        if(s->wr){irq_restore(q);
            return pipe_error(flags);
        }if(s->error){i32 e=s->error;
            s->error=0;
            irq_restore(q);
            return -e;
        }
        if(s->type!=N_STREAM){if(len>(s->raw?65515:65507)){irq_restore(q);
                return -EMSGSIZE;
            }if(!to&&!s->connected){irq_restore(q);
                return -EDESTADDRREQ;
            }
            struct pbuf*p=pbuf_alloc(PBUF_TRANSPORT,(u16)len,PBUF_RAM);
            if(!p){irq_restore(q);
                return -ENOMEM;
            }pbuf_take(p,buf,(u16)len);
            ip_addr_t ip=ip_address(to?to->address:0);
            err_t e=s->raw?(to?raw_sendto(s->raw,p,&ip):raw_send(s->raw,p)):
                to?udp_sendto(s->udp,p,&ip,lwip_ntohs(to->port)):udp_send(s->udp,p);
            pbuf_free(p);
            irq_restore(q);
            return e?-error_code(e):(i64)len;
        }
        if(!s->connected){irq_restore(q);
            return -ENOTCONN;
        }if(!s->tcp){irq_restore(q);
            return pipe_error(flags);
        }if(!len){irq_restore(q);
            return 0;
        }
        u32 n=tcp_sndbuf(s->tcp);
        u32 queued=TCP_SND_BUF-n;
        u32 room=s->slimit>queued?s->slimit-queued:0;
        if(n>room)n=room;
        if(n>len)n=(u32)len;
        if(n){err_t e=tcp_write(s->tcp,buf,(u16)n,TCP_WRITE_FLAG_COPY);
            if(e==ERR_OK){tcp_output(s->tcp);
                irq_restore(q);
                return n;
            }if(e!=ERR_MEM){irq_restore(q);
                return -error_code(e);
            }}
        irq_restore(q);
        i32 r=wait_for(f,s,flags,s->stime,start);
        if(r)return r;
    }
}
i64 inet_recv(file_t*f,void*buf,u64 len,u32 flags,inet_addr_t*from,u32*out_flags) {
    if(flags&~(N_PEEK|N_TRUNC|N_DONTWAIT|N_WAITALL))return -EOPNOTSUPP;
    endpoint_t*s=ep(f);
    u64 start=time_counter_ns()/1000000ULL;
    u64 done=0;
    if(out_flags)*out_flags=0;
    if(s->type==1&&!len)return 0;
    for(;;){u64 q=irq_save();
        packet_t*m=s->head;
        if(m){u32 left=m->p->tot_len-m->off;
            u32 n=len-done<left?(u32)(len-done):left;
            pbuf_copy_partial(m->p,(u8*)buf+done,(u16)n,(u16)m->off);
            if(from){if(s->type!=N_STREAM)*from=m->from;
                else inet_name(f,from,true);
            }
            if(s->type!=N_STREAM&&n<left&&out_flags)*out_flags|=N_TRUNC;
            u32 result=s->type!=N_STREAM&&(flags&N_TRUNC)?left:n;
            if(!(flags&N_PEEK)){s->bytes-=s->type!=N_STREAM?left:n;
                m->off+=n;
                if(s->type==1&&s->tcp)tcp_recved(s->tcp,(u16)n);
                if(s->type!=N_STREAM||m->off==m->p->tot_len){s->head=m->next;
                    if(!s->head)s->tail=0;
                    s->packets--;
                    pbuf_free(m->p);
                    kfree(m);
                }}
            done+=result;
            irq_restore(q);
            if(s->type!=N_STREAM||done==len||(flags&N_PEEK)||!(flags&N_WAITALL))return (i64)done;
            continue;
        }
        if(s->rd){irq_restore(q);
            return (i64)done;
        }
        if(s->error){i32 e=s->error;
            s->error=0;
            irq_restore(q);
            return done?(i64)done:-e;
        }
        if(s->eof){irq_restore(q);
            return (i64)done;
        }
        if(s->type==1&&!s->connected){irq_restore(q);
            return -ENOTCONN;
        }
        irq_restore(q);
        i32 r=wait_for(f,s,flags,s->rtime,start);
        if(r)return done?(i64)done:r;
    }
}
static i64 inet_read(file_t*f,void*b,u64 n) {return inet_recv(f,b,n,0,0,0);
}
static i64 inet_write(file_t*f,const void*b,u64 n) {return inet_send(f,b,n,0,0);
}
static void inet_release(file_t*f) {u64 q=irq_save();
    destroy(ep(f));
    f->priv=0;
    irq_restore(q);
}
static u32 inet_poll(file_t*f) {
    u64 q=irq_save();
    endpoint_t*s=ep(f);
    u32 r=0;
    if(s->error)r|=POLLERR;
    if(s->head||s->count||s->eof||s->rd)r|=POLLIN;
    if(s->type!=N_STREAM||(!s->listening&&!s->connecting&&(s->error||s->eof||s->wr||(s->connected&&(!s->tcp||(tcp_sndbuf(s->tcp)&&(u32)TCP_SND_BUF-tcp_sndbuf(s->tcp)<s->slimit))))))r|=POLLOUT;
    if(s->eof&&s->wr)r|=POLLHUP;
    irq_restore(q);
    return r;
}
static const file_ops_t inet_ops={inet_read,inet_write,inet_release,inet_poll};
i32 inet_option(file_t*f,bool set,u32 option,void*value,u32*length) {
    endpoint_t*s=ep(f);
    u32 need=(option==INET_RCVTIMEO||option==INET_SNDTIMEO)?8:4;
    if(!value||!length||*length<need)return -EINVAL;
    u64 q=irq_save();
    i32 v=0;
    memcpy(&v,value,4);
    i32 result=0;
    struct ip_pcb*p=s->raw?(struct ip_pcb*)s->raw:s->type==1?(struct ip_pcb*)s->tcp:(struct ip_pcb*)s->udp;
    if(need==8){u64*t=option==INET_RCVTIMEO?&s->rtime:&s->stime;
        if(set){u64 n;
            memcpy(&n,value,8);
            *t=n;
        }else memcpy(value,t,8);
    }
    else switch(option){
        case INET_TTL:case INET_TOS:case INET_MULTICAST_TTL:{
            if(!p){result=-ENOTCONN;break;}
            u8*field=option==INET_TTL?&p->ttl:option==INET_TOS?&p->tos:0;
            if(!field){
                if(s->raw)field=&s->raw->mcast_ttl;
                else if(s->udp)field=&s->udp->mcast_ttl;
                else {result=-EOPNOTSUPP;break;}
            }
            if(set){if(v<(option==INET_TTL?1:0)||v>255)result=-EINVAL;
                else *field=(u8)v;
            }else v=*field;
            break;
        }

        case INET_TYPE:if(set)result=-EINVAL;
        else v=s->type;
        break;
        case INET_ACCEPTCONN:if(set)result=-EINVAL;
        else v=s->listening;
        break;
        case INET_ERROR:if(set)result=-EINVAL;
        else{v=s->error;
            s->error=0;
        }break;
        case INET_RCVBUF:case INET_SNDBUF:{u32*n=option==INET_RCVBUF?&s->rlimit:&s->slimit;
            if(set){if(v<=0)result=-EINVAL;
                else{u32 max=option==INET_RCVBUF?RX_LIMIT:TCP_SND_BUF;
                    *n=(u32)v>max?max:(u32)v;
                    // lwIP can deliver a whole receive window as one pbuf.
                    // An empty stream queue must always accept that callback;
                    // otherwise returning ERR_MEM would prevent any progress.
                    if(option==INET_RCVBUF){
                        if(s->type==N_STREAM&&*n<TCP_WND)*n=TCP_WND;
                        if(*n<s->bytes)*n=s->bytes;
                    }
                }}else v=(i32)*n;
            break;
        }
        case INET_REUSEADDR:case INET_KEEPALIVE:case INET_BROADCAST:{u8 bit=option==INET_REUSEADDR?SOF_REUSEADDR:option==INET_KEEPALIVE?SOF_KEEPALIVE:SOF_BROADCAST;
            if(!p){result=-ENOTCONN;
                break;
            }if(set){if(v)ip_set_option(p,bit);
                else ip_reset_option(p,bit);
            }else v=ip_get_option(p,bit)!=0;
            break;
        }
        case INET_NODELAY:if(s->type!=1){result=-EOPNOTSUPP;
            break;
        }if(!s->tcp||s->listening){result=-EINVAL;
            break;
        }if(set){if(v)tcp_nagle_disable(s->tcp);
            else tcp_nagle_enable(s->tcp);
        }else v=tcp_nagle_disabled(s->tcp)!=0;
        break;
        default:result=-92;
        break;
    }
    if(!set&&need==4&&!result)memcpy(value,&v,4);
    *length=need;
    irq_restore(q);
    return result;
}
