// This file has been generated in whole or in part by a Large Language Model
// Firmware-event model tests control/data integration; no physical-radio claim.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <iwx200.h>
#include <iwx200_station.h>
#include <wifi_crypto.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "wifi_wpa_vectors.h"
#pragma GCC diagnostic pop
#include "../../drivers/iwx200_station.c"
static const u8 ap[6]={0,0x1a,0x2b,0x3c,0x4d,0x5e},sta[6]={2,0x11,0x22,0x33,0x44,0x55};
static iwx200_notification_fn notification;
static bool secured,drop_assoc,fail_m4;
static u32 scan_uid;
static int pending_scan,pending_session,pending_mgmt,pending_eapol;
static u32 tx_count,m2_count,m4_count;
static u8 last_tx[2400];static u32 last_length;
static u64 clock_ns;
static u8 energy=50;
static bool transport_online=true;
u64 time_counter_ns(void) {return clock_ns+=1000000;}
i64 random_read(void* p,u64 n) {for(u64 i=0;i<n;i++) ((u8*)p)[i]=(u8)(0x5a+i);return (i64)n;}
bool iwx200_online(void) {return transport_online;}
i32 iwx200_run_init(void) {return 0;}
i32 iwx200_configure_radio(void) {return 0;}
void iwx200_nvm_mac(u8 out[6]) {memcpy(out,sta,6);}
bool iwx200_get_nvm(iwx200_nvm_t* out) {memset(out,0,sizeof(*out));out->tx_chains=out->rx_chains=3;out->channel_count=110;out->channel_profiles[0]=9;out->channel_profiles[109]=9;return true;}
void iwx200_set_notification_handler(iwx200_notification_fn cb) {notification=cb;}
bool iwx200_has_capability(u32 bit) { return bit==48; }
i32 iwx200_cmd_version(u8 group,u8 op) {
 if(group==3) return op==5?1:-1;
 switch(op) {case 8:return 3;case 0xc:return 5;case 0xd:return 15;case 0x28:return 5;case 0x2b:return 2;case 0x18:return 12;
 case 0x19:return 2;case 0x1c:return 9;case 0xc8:case 0xd0:return 1;default:return -1;}
}
i32 iwx200_notif_version(u8 group,u8 op) {return group==3&&op==0xfb?2:op==0x1c?7:op==0xc8?5:-1;}
static void put16(u8* p,u32 n) {p[0]=(u8)n;p[1]=(u8)(n>>8);}
static void inject(const u8* frame,u32 length) {
 u8 rx[48+2400]={0};assert(length<=2400);put16(rx,length);rx[12]=3;rx[32]=energy;rx[33]=60;rx[34]=1;memcpy(rx+48,frame,length);
 notification(0,0xc1,rx,48+length);
}
static u32 header(u8* p,u8 fc,u8 flags) {
 memset(p,0,24);p[0]=fc;p[1]=flags;memcpy(p+4,sta,6);memcpy(p+10,ap,6);memcpy(p+16,ap,6);return 24;
}
static void scan_events(void) {
 u8 beacon[128];u32 n=header(beacon,0x80,0);memset(beacon+4,255,6);memset(beacon+n,0,12);beacon[n+8]=100;beacon[n+10]=secured?0x11:1;n+=12;
 beacon[n++]=0;beacon[n++]=4;memcpy(beacon+n,"IEEE",4);n+=4;
 const u8 rates[]={1,4,0x82,0x84,0x8b,0x96,3,1,1,5,4,0,2,0,0};memcpy(beacon+n,rates,sizeof(rates));n+=sizeof(rates);
 if(secured) {memcpy(beacon+n,v_rsn,sizeof(v_rsn));n+=sizeof(v_rsn);}
 inject(beacon,n);
 if(secured) {
  // Stronger open BSS using the same SSID must not capture a password join.
  beacon[34]=1;beacon[15]^=1;beacon[21]^=1;energy=30;
  inject(beacon,n-sizeof(v_rsn));energy=50;
 }
 u8 done[16]={0};memcpy(done,&scan_uid,4);done[6]=1;notification(0,0xf,done,sizeof(done));
}
void iwx200_poll(void) {
 if(pending_scan) {pending_scan=0;scan_events();return;}
 if(pending_session) {pending_session=0;u8 p[16]={0};p[4]=1;p[8]=1;notification(3,0xfb,p,sizeof(p));return;}
 if(pending_mgmt) {
  int kind=pending_mgmt;pending_mgmt=0;u8 f[64];u32 n=header(f,kind==1?0xb0:0x10,0);
  memset(f+n,0,6);if(kind==1) f[n+2]=2;else {if(drop_assoc) return;f[n+4]=1;f[n+5]=0xc0;}
  inject(f,n+6);return;
 }
 if(pending_eapol) {
  int kind=pending_eapol;pending_eapol=0;u8 f[512];u32 n=header(f,8,2);
  const u8 snap[]={0xaa,0xaa,3,0,0,0,0x88,0x8e};memcpy(f+n,snap,8);n+=8;
  const u8* p=kind==1?v_m1:v_m3;u32 length=kind==1?sizeof(v_m1):sizeof(v_m3);memcpy(f+n,p,length);inject(f,n+length);
 }
}
i32 iwx200_command(u32 group,u32 op,u32 version,const void* data,u32 length,void* out,u32 capacity) {
 assert(version==0);const u8* p=data;
 if(op==0xc8) {assert(length==28&&p[0]=='Z'&&p[1]=='Z'&&p[2]==0x10&&capacity>=460);memset(out,0,460);((u8*)out)[16]=110;((u8*)out)[20]=9;return 460;}
 if(op==0xd) {assert(length==1940);memcpy(&scan_uid,p,4);pending_scan=1;return 0;}
 if(op==8&&group!=5) {assert(length==32&&p[8]==1&&!p[9]&&!p[10]&&!p[11]&&p[12]==1&&p[13]==0&&p[14]==0);assert(p[20]==6&&p[21]==0x14);} // UHB layout
 if(op==0x28) {assert(length==148&&p[8]==5&&!memcmp(p+16,sta,6)&&!memcmp(p+24,ap,6));if(p[4]==2&&secured) pending_eapol=1;}
 if(op==0x18) {assert(length==48&&p[16]==0&&p[27]==0x3c);assert(capacity==4);memset(out,0,4);((u8*)out)[0]=1;return 4;}
 if(op==0x2b) {assert(length==28&&p[12]==255&&p[16]==255&&capacity==4);memset(out,0,4);return 4;}
 if(group==3&&op==5) {assert(length==24);if(p[4]==1) pending_session=1;}
 return 0;
}
i32 iwx200_data_queue_enable(u8 station,u8 tid) {assert(station==0&&tid==15);return 7;}
i32 iwx200_data_queue_disable(u8 q) {assert(q==7);return 0;}
bool iwx200_data_tx(u8 q,const u8 prefix[20],const void* frame,u32 length) {
 const u8* f=frame;assert(q==7&&length<=2312&&prefix[16]==0&&prefix[17]==0x40);tx_count++;
 memcpy(last_tx,frame,length);last_length=length;
 if(f[0]==8&&(f[1]&0x40)) {
  u8 aad[22],pn[6]={f[24],f[25],f[28],f[29],f[30],f[31]},plain[512];
  aad[0]=f[0]&0x8f;aad[1]=f[1]&0xc7;memcpy(aad+2,f+4,18);aad[20]=f[22]&15;aad[21]=0;
  u32 plain_length=sizeof(plain);assert(wifi_ccmp_decrypt(v_ptk+32,pn,sta,aad,22,f+32,length-32,plain,&plain_length));
  if(plain_length>=8&&plain[6]==0x88&&plain[7]==0x8e) {
   assert((plain_length==8+sizeof(v_m4)&&!memcmp(plain+8,v_m4,sizeof(v_m4)))||
          (plain_length==8+sizeof(v_group_ack)&&!memcmp(plain+8,v_group_ack,sizeof(v_group_ack))));
  }
 }
 if(f[0]==0xb0) pending_mgmt=1;
 else if(f[0]==0) {pending_mgmt=2;assert(!secured||memmem(f,length,v_rsn,sizeof(v_rsn)));}
 else if(length>=32&&f[30]==0x88&&f[31]==0x8e) {
  if(length==32+sizeof(v_m2)) {assert(!memcmp(f+32,v_m2,sizeof(v_m2)));m2_count++;pending_eapol=2;}
  else {assert(length==32+sizeof(v_m4)&&!memcmp(f+32,v_m4,sizeof(v_m4)));m4_count++;if(fail_m4) return false;}
 }
 return true;
}
static void inject_eapol(const u8* p,u32 length) {
 u8 f[512];u32 n=header(f,8,2);const u8 snap[]={0xaa,0xaa,3,0,0,0,0x88,0x8e};
 memcpy(f+n,snap,8);n+=8;memcpy(f+n,p,length);inject(f,n+length);
}
static void inbound_data(bool encrypted,bool group,u64 number,bool bad_mic) {
 u8 f[256],body[]={0xaa,0xaa,3,0,0,0,8,0,1,2,3,4};u32 n=header(f,8,2);
 if(group) memset(f+4,255,6);
 if(encrypted) {
  f[1]|=0x40;u8 pn[6],aad[22];for(u32 i=0;i<6;i++) pn[i]=(u8)(number>>(8*i));
  aad[0]=8;aad[1]=0x42;memcpy(aad+2,f+4,18);aad[20]=aad[21]=0;
  f[n++]=pn[0];f[n++]=pn[1];f[n++]=0;f[n++]=group?0xa0:0x20;memcpy(f+n,pn+2,4);n+=4;
  u32 out=sizeof(f)-n;assert(wifi_ccmp_encrypt(group?v_gtk:v_ptk+32,pn,ap,aad,22,body,sizeof(body),f+n,&out));n+=out;if(bad_mic) f[n-1]^=1;
 } else {memcpy(f+n,body,sizeof(body));n+=sizeof(body);}
 inject(f,n);
}
int main(void) {
 iwx_scan_result result[2];assert(iwx200_scan(result,2)==1);assert(!memcmp(result[0].ssid,"IEEE",4)&&result[0].channel==1&&result[0].signal_dbm==-50);
 assert(iwx200_connect("IEEE",4,0)==0&&iwx200_link()==IWX_LINK_UP);
 u8 eth[32]={0};memcpy(eth,ap,6);memcpy(eth+6,sta,6);eth[12]=8;
 assert(iwx200_send(eth,sizeof(eth))&&last_length==50&&last_tx[0]==8&&last_tx[1]==1);
 inbound_data(false,false,0,false);u8 received[1514];assert(iwx200_receive(received,sizeof(received))==18&&received[14]==1);
 iwx200_disconnect();assert(iwx200_link()==IWX_LINK_DOWN);
 drop_assoc=true;assert(iwx200_connect("IEEE",4,0)==-110&&iwx200_link()==IWX_LINK_DOWN);drop_assoc=false;
 secured=true;assert(iwx200_connect("IEEE",4,"password")==0&&iwx200_link()==IWX_LINK_UP&&m2_count==1&&m4_count==1);
 assert(iwx200_send(eth,sizeof(eth))&&last_length==66&&(last_tx[1]&0x40)&&last_tx[27]==0x20);
 inbound_data(false,false,0,false);assert(!iwx200_receive(received,sizeof(received)));
 inbound_data(true,false,1,true);assert(!iwx200_receive(received,sizeof(received)));
 inbound_data(true,false,1,false);assert(iwx200_receive(received,sizeof(received))==18);
 inbound_data(true,false,1,false);assert(!iwx200_receive(received,sizeof(received)));
 inbound_data(true,true,1,false);assert(iwx200_receive(received,sizeof(received))==18);
 inbound_data(true,true,1,false);assert(!iwx200_receive(received,sizeof(received)));
 u64 saved_pn=connection.wpa.tx_pn;u32 saved_generation=connection.wpa.ptk_generation;
 inject_eapol(v_m1,sizeof(v_m1));assert(!iwx200_receive(received,sizeof(received))&&iwx200_link()==IWX_LINK_UP);
 inject_eapol(v_m3,sizeof(v_m3));assert(!iwx200_receive(received,sizeof(received))&&iwx200_link()==IWX_LINK_UP);
 assert(!(last_tx[1]&0x40));
 assert(connection.wpa.tx_pn==saved_pn&&connection.wpa.ptk_generation==saved_generation);
 inject_eapol(v_group,sizeof(v_group));assert(!iwx200_receive(received,sizeof(received))&&iwx200_link()==IWX_LINK_UP);
 assert(last_tx[1]&0x40);
 assert(connection.wpa.gtk_index==1&&!memcmp(connection.wpa.gtk,v_gtk2,16));
 u8 new_m1[sizeof(v_m1)];memcpy(new_m1,v_m1,sizeof(new_m1));new_m1[16]=7;u32 saved_tx=tx_count;
 inject_eapol(new_m1,sizeof(new_m1));assert(!iwx200_receive(received,sizeof(received))&&iwx200_link()==IWX_LINK_DOWN);
 assert(tx_count==saved_tx&&!iwx200_send(eth,sizeof(eth)));
 wpa_t empty={0};assert(!memcmp(&connection.wpa,&empty,sizeof(empty)));
 iwx200_disconnect();fail_m4=true;assert(iwx200_connect("IEEE",4,"password")<0&&iwx200_link()==IWX_LINK_DOWN);
 fail_m4=false;assert(iwx200_connect("IEEE",4,"password")==0);
 transport_online=false;assert(iwx200_link()==IWX_LINK_DOWN&&!iwx200_send(eth,sizeof(eth)));
 assert(!memcmp(&connection.wpa,&empty,sizeof(empty)));
 assert(tx_count>8);puts("iwx station association/data flow: ok");
}
