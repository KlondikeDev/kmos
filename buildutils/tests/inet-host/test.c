// This file has been generated in whole or in part by a Large Language Model
#include <inet.h>
#include <signal.h>
#include <lwip/timeouts.h>
#include <lwip/sys.h>
#include <lwip/inet_chksum.h>
extern void* calloc(size_t,size_t);
extern void free(void*);
extern int printf(const char*,...);
extern void abort(void);
extern int fflush(void*);
i64 random_read(void*buffer,u64 length) {(void)buffer;(void)length;return -11;}
volatile u32 ticks;
static u64 clock_ns;
static bool clock_stall;
static u32 timed_sleeps, largest_tick_wait;
u64 time_counter_ns(void) {return clock_ns;}
static void clock_timeout(void*arg) {*(bool*)arg=true;}
int file_poll_wait;
static process_t process;
static bool interrupted;
static u32 signals;
void* kmalloc(u64 n) { return calloc(1,n); }
void kfree(void* p) { free(p); }
process_t* proc_current(void) { return &process; }
bool signal_pending(const process_t* p) { (void)p; return interrupted; }
void signal_send(process_t* p,i32 s) { (void)p;if(s==SIGPIPE)signals++; }
void sched_wake_obj(const void* p) { (void)p; }
void sched_block_on(const void* p) { (void)p; net_poll(); ticks++;clock_ns+=clock_stall?100000000ULL:4000000ULL; }
bool sched_block_on_until(const void* p,u32 d) { timed_sleeps++;u32 delta=d-ticks;if(delta>largest_tick_wait)largest_tick_wait=delta;sched_block_on(p); return (i32)(ticks-d)<0; }
static bool wired, wired_link;
bool e1000_init(void) { return wired; }
bool e1000_link(void) { return wired_link; }
void e1000_mac(u8 m[6]) { for(int i=0;i<6;i++)m[i]=0; }
bool e1000_send(const void* p,u32 n) { (void)p;(void)n;return false; }
u32 e1000_receive(void* p,u32 n) { (void)p;(void)n;return 0; }
static u32 early_wifi_probes;
static bool wifi_ready, wifi_link;
bool iwx200_init(void) { early_wifi_probes++;return false; }
bool iwx200_online(void) { return wifi_ready; }
void iwx200_mac(u8 m[6]) { for(int i=0;i<6;i++)m[i]=0; }
int iwx200_link(void) { return wifi_link?3:0; }
bool iwx200_send(const void* p,u32 n) { (void)p;(void)n;return false; }
u32 iwx200_receive(void* p,u32 n) { (void)p;(void)n;return 0; }
#define CHECK(x) do { if(!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); fflush(0); abort(); } } while(0)
/* Real lwIP loopback exercises IP header delivery and the ICMP responder. */
static void icmp_test(void) {
 file_t *a,*b;inet_addr_t loop={0x0100007f,0,0},from;
 process.euid=1000;CHECK(inet_create(3,O_NONBLOCK,1,&a)==-1);process.euid=0;
 CHECK(inet_create(3,O_NONBLOCK,17,&a)==-93);
 CHECK(inet_create(3,O_NONBLOCK,1,&a)==0);
 CHECK(inet_create(3,O_NONBLOCK,1,&b)==0);
 CHECK(inet_bind(a,&loop)==0);
 CHECK(inet_connect(b,&loop)==0);
 CHECK(inet_name(b,&from,true)==0 && from.address==loop.address && !from.port);
 CHECK(inet_listen(a,1)==-95);
 CHECK((a->ops->poll(a)&4)!=0);
 i32 ttl=42,tos=16,got=0;u32 optlen=4;
 CHECK(inet_option(a,true,INET_TTL,&ttl,&optlen)==0);
 CHECK(inet_option(a,true,INET_TOS,&tos,&optlen)==0);
 CHECK(inet_option(a,false,INET_TTL,&got,&optlen)==0 && got==42);
 ttl=0;CHECK(inet_option(a,true,INET_TTL,&ttl,&optlen)==-22);
 ttl=256;CHECK(inet_option(a,true,INET_TTL,&ttl,&optlen)==-22);
 ttl=7;CHECK(inet_option(a,true,INET_MULTICAST_TTL,&ttl,&optlen)==0);
 CHECK(inet_option(a,false,INET_MULTICAST_TTL,&got,&optlen)==0 && got==7);
 u8 request[12]={8,0,0,0,0x12,0x34,0,1,'p','i','n','g'};
 u16 checksum=inet_chksum(request,sizeof request);__builtin_memcpy(request+2,&checksum,2);
 CHECK(inet_send(a,request,sizeof request,0,&loop)==sizeof request);
 net_poll();
 u8 reply[80];bool got_a=false,got_b=false;
 for(u32 i=0;i<3;i++) {
  i64 n=inet_recv(a,reply,sizeof reply,0,&from,0);if(n==-11)break;
  CHECK(n==32 && (reply[0]>>4)==4 && reply[9]==1);
  if(reply[20]==8)CHECK(reply[8]==42 && reply[1]==16);
  CHECK(from.address==loop.address && from.port==0);
  u32 off=(reply[0]&15)*4;
  CHECK(inet_chksum(reply+off,n-off)==0);
  if(reply[off]==0){CHECK(reply[off+4]==0x12 && reply[off+5]==0x34 && reply[n-1]=='g');got_a=true;}
 }
 for(u32 i=0;i<3;i++) {
  i64 n=inet_recv(b,reply,sizeof reply,0,&from,0);if(n==-11)break;
  CHECK(n==32);if(reply[20]==0)got_b=true;
 }
 CHECK(got_a && got_b);
 /* Connected raw sockets receive only packets from the selected source. */
 inet_addr_t other={0x0200007f,0,0};
 CHECK(inet_connect(b,&other)==0);
 CHECK(inet_send(a,request,sizeof request,0,&loop)==sizeof request);net_poll();
 CHECK(inet_recv(b,reply,sizeof reply,0,0,0)==-11);
 while(inet_recv(a,reply,sizeof reply,0,0,0)>=0){}
 CHECK(inet_connect(b,&loop)==0);
 /* A small queue drops whole packets without corrupting another socket. */
 i32 limit=32;u32 length=4;CHECK(inet_option(a,true,INET_RCVBUF,&limit,&length)==0);
 CHECK(inet_send(b,request,sizeof request,0,0)==sizeof request);net_poll();
 u32 flags=0;CHECK(inet_recv(a,reply,4,2,&from,&flags)==4 && flags==32);
 CHECK(inet_recv(a,reply,4,32,&from,&flags)==32 && flags==32);
 CHECK(inet_recv(a,reply,sizeof reply,0,0,0)==-11);
 CHECK(inet_send(a,request,65516,0,&loop)==-90);
 a->ops->release(a);kfree(a);b->ops->release(b);kfree(b);
 /* Raw PCB storage is finite, and close releases it for reuse. */
 file_t* files[64];u32 count=0;
 while(count<64 && inet_create(3,O_NONBLOCK,1,&files[count])==0)count++;
 CHECK(count>0 && count<64);
 for(u32 i=0;i<count;i++){files[i]->ops->release(files[i]);kfree(files[i]);}
 CHECK(inet_create(3,O_NONBLOCK,1,&a)==0);a->ops->release(a);kfree(a);
}
int main(int argc,char** argv) {
 (void)argv;wired=argc>1;wired_link=wired;
 clock_ns=((1ULL<<32)-5)*1000000ULL;
 net_init();
 CHECK(early_wifi_probes==0);
 icmp_test();
 CHECK(sys_now()==0xfffffffbU);
 bool timeout_fired=false;u32 old_ticks=ticks;
 sys_timeout(10,clock_timeout,&timeout_fired);
 clock_ns+=4000000ULL;net_poll();CHECK(!timeout_fired && sys_now()==0xffffffffU);
 clock_ns+=6000000ULL;net_poll();CHECK(timeout_fired && ticks==old_ticks && sys_now()==5);
 file_t *a,*b,*l,*c;
 inet_addr_t dest={0x0100007f,0x2823,0},src;
 CHECK(inet_create(2,O_NONBLOCK,0,&a)==0);
 CHECK(inet_create(2,O_NONBLOCK,0,&b)==0);
 CHECK(inet_bind(b,&dest)==0);
 CHECK(inet_recv(b,&src,1,0,0,0)==-11);
 CHECK(inet_send(a,"hello",5,0,&dest)==5); net_poll();
 char data[16];u32 flags=0;
 CHECK(inet_recv(b,data,2,2,&src,&flags)==2 && flags==32);
 CHECK(inet_recv(b,data,16,0,&src,&flags)==5 && data[4]=='o');
 CHECK(inet_send(a,"",0,0,&dest)==0);net_poll();
 CHECK(inet_recv(b,data,16,0,0,0)==0);
 i32 small=8;u32 optlen=4;
 CHECK(inet_option(b,true,INET_RCVBUF,&small,&optlen)==0);
 CHECK(inet_send(a,"12345678",8,0,&dest)==8);net_poll();
 CHECK(inet_send(a,"dropped",7,0,&dest)==7);net_poll();
 CHECK(inet_recv(b,data,16,0,0,0)==8);
 CHECK(inet_recv(b,data,16,0,0,0)==-11);
 u64 timeout=8;optlen=8;CHECK(inet_option(b,true,INET_RCVTIMEO,&timeout,&optlen)==0);
 b->flags&=~O_NONBLOCK;CHECK(inet_recv(b,data,16,0,0,0)==-11);
 clock_stall=true;timed_sleeps=largest_tick_wait=0;
 CHECK(inet_recv(b,data,16,0,0,0)==-11);
 CHECK(timed_sleeps==1 && largest_tick_wait<=5);
 timeout=1000;optlen=8;CHECK(inet_option(b,true,INET_RCVTIMEO,&timeout,&optlen)==0);
 timed_sleeps=largest_tick_wait=0;CHECK(inet_recv(b,data,16,0,0,0)==-11);
 CHECK(timed_sleeps==10 && largest_tick_wait==5);
 clock_stall=false;
 interrupted=true;CHECK(inet_recv(b,data,16,0,0,0)==-4);interrupted=false;
 a->ops->release(a);kfree(a);
 CHECK(inet_create(1,O_NONBLOCK,0,&l)==0);
 dest.port=0x2923;CHECK(inet_bind(l,&dest)==0);CHECK(inet_listen(l,4)==0);
 CHECK(inet_create(1,O_NONBLOCK,0,&c)==0);
 CHECK(inet_connect(c,&dest)==-115);net_poll();
 CHECK(inet_accept(l,O_NONBLOCK,&a,&src)==0);
 CHECK((c->ops->poll(c)&4)!=0);
 small=8;optlen=4;
 CHECK(inet_option(a,true,INET_RCVBUF,&small,&optlen)==0);
 CHECK(inet_send(c,"123456789",9,0,0)==9);net_poll();
 CHECK(inet_recv(a,data,sizeof(data),0,0,0)==9 && data[8]=='9');
 i32 nodelay=1;optlen=4;CHECK(inet_option(c,true,INET_NODELAY,&nodelay,&optlen)==0);
 CHECK(inet_send(c,"stream",6,0,0)==6);net_poll();
 CHECK(inet_recv(a,data,3,0,0,0)==3 && data[2]=='r');
 CHECK(inet_shutdown(c,1)==0);net_poll();
 CHECK(inet_recv(a,data,16,0,0,0)==3 && data[2]=='m');
 CHECK(inet_recv(a,data,16,0,0,0)==0);
 CHECK(inet_shutdown(a,1)==0);net_poll();
 CHECK(inet_recv(a,data,16,0,0,0)==0);
 CHECK(inet_shutdown(a,2)==0);net_poll();
 for(u32 i=0;i<130;i++){ticks+=250;clock_ns+=1000000000ULL;net_poll();}
 CHECK(inet_send(a,"x",1,16384,0)==-32 && signals==0);
 CHECK(inet_send(a,"x",1,0,0)==-32 && signals==1);
 a->ops->release(a);kfree(a); c->ops->release(c);kfree(c);
 CHECK(inet_create(1,O_NONBLOCK,0,&c)==0);dest.port=0x3023;
 CHECK(inet_connect(c,&dest)==-115);net_poll();
 i32 socket_error=0;optlen=4;CHECK(inet_option(c,false,INET_ERROR,&socket_error,&optlen)==0 && socket_error==111);
 CHECK(inet_option(c,false,INET_ERROR,&socket_error,&optlen)==0 && socket_error==0);
 c->ops->release(c);kfree(c);
 l->ops->release(l);kfree(l);b->ops->release(b);kfree(b);
 wired_link=false;wifi_ready=true;net_poll();
 net_info_t info;net_info(&info);CHECK(info.up && !info.link && info.mtu==1500);
 char name[16];net_ifname(name);CHECK(name[0]==(wired?'e':'w'));
 if(wired){wired_link=true;net_poll();}
 wifi_link=true;net_poll();net_info(&info);CHECK(info.link);
 if(wired){wired_link=false;net_poll();net_info(&info);CHECK(info.link);net_ifname(name);CHECK(name[0]=='w');wired_link=true;net_poll();net_ifname(name);CHECK(name[0]=='e');wired_link=false;net_poll();}
 wifi_link=false;net_poll();net_info(&info);CHECK(!info.link && !info.address);
 printf("inet host: ICMP echo/privileges/demux/bounds/options, loopback UDP/TCP, peek/truncation, zero datagram, half-close, TSC timers/wrap/timeouts PASS\n");
 return 0;
}
