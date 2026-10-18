// This file has been generated in whole or in part by a Large Language Model
// AX200 station support. Wire layouts in iwx_station_hw.h retain the Intel
// BSD license. Reference: OpenBSD if_iwx.c/if_iwxreg.h, iwx_umac_scan_v14,
// iwx_rx_mpdu, iwx_tx. This baseline deliberately uses passive scans.
#include <iwx200_station.h>
#include <iwx_station_hw.h>
#include <iwx200.h>
#include <wifi.h>
#include <wifi_wpa.h>
#include <wifi_crypto.h>
// Join failure diagnostics: source line of the failure site, a site-specific
// value (deauth reason, firmware status, ...) and the 802.11 state then.
static iwx_station_diag_t diag;
void iwx_station_diag(iwx_station_diag_t* out) { if(out) *out=diag; }
static i32 diag_fail(u32 line,u32 extra,i32 e) {
 if(!diag.line) {diag.line=line;diag.extra=extra;diag.error=e;}
 return e;
}
extern u64 time_counter_ns(void);

#define EINVAL 22
#define EIO 5
#define EBUSY 16
#define EOPNOTSUPP 95
#define ENODEV 19
#define ETIMEDOUT 110
static void clear(void* p,u32 n) { u8* q=p; while(n--) *q++=0; }
static void copy(void* d,const void* s,u32 n) { u8* q=d;const u8* p=s;while(n--) *q++=*p++; }
static bool equal(const void* a,const void* b,u32 n) { const u8* p=a;const u8* q=b;while(n--) if(*p++!=*q++) return false;return true; }
static u16 le16(const u8* p) { return (u16)(p[0]|((u16)p[1]<<8)); }
static u32 le32(const u8* p) { return (u32)le16(p)|((u32)le16(p+2)<<16); }
static u64 le64(const u8* p) { return (u64)le32(p)|((u64)le32(p+4)<<32); }
static bool valid_mac(const u8* p) { static const u8 zero[6]={0}; return p&&!(p[0]&1)&&!equal(p,zero,6); }
// Intel AX200 NVM channel index order (OpenBSD iwx_nvm_channels_8000).
// This list supplies numbers only: profiles authorize each channel.
static const u8 channel_numbers[51]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,
 36,40,44,48,52,56,60,64,68,72,76,80,84,88,92,96,100,104,108,112,116,
 120,124,128,132,136,140,144,149,153,157,161,165,169,173,177,181};
static struct {
 bool configured, scanning;
 u8 mac[6];u32 tx_chains,rx_chains,profile_count;
 u32 nvm_profiles[51],profiles[51];
 iwx_scan_result_t results[IWX_STATION_RESULTS];u32 result_count;
 i32 scan_error;
 u32 scan_uid;
} station;

bool iwx_station_build_tx(void* prefix,u32 capacity,const void* frame,u32 length,
                          u8 antenna,bool band_2ghz,u8 rate_version) {
 if((rate_version!=1&&rate_version!=2)||!prefix||capacity<20||!frame||length<24||length>IWX_STATION_FRAME_MAX||!antenna||(antenna&~3)) return false;
 const u8* f=frame;
 if((f[0]&3)||((f[0]>>2)&3)==1||((f[1]&3)==3)) return false;
 u32 header=24+((((f[0]>>2)&3)==2&&(f[0]&0x80))?2:0);
 if(length<header) return false;
 struct iwx_station_tx_cmd* cmd=prefix;clear(cmd,sizeof(*cmd));
 cmd->len=(u16)length;
 cmd->offload_assist=(u16)((header/2)<<8);
 if(header&3) cmd->offload_assist|=1u<<13;
 // Encryption is performed by the software supplicant. The NIC must never
 // apply a second CCMP layer, including when the Protected bit is set.
 cmd->flags=3;
 if((f[0]&0x0c)==0) cmd->flags|=4;
 // Legacy v1 rate encoding: CCK 1Mbps PLCP10 in 2.4GHz; OFDM6 PLCP13.
 // ANT_A/ANT_B are at bit14. Never select a chain outside NVM mask.
 cmd->rate_n_flags=(rate_version==2?(band_2ghz?0u:(1u<<8)):(band_2ghz?(10u|(1u<<9)):13u))|((u32)(antenna&((u8)(0u-antenna)))<<14);
 return true;
}

u32 iwx_station_decode_rx(const void* payload,u32 length,void* output,u32 capacity,
                          u8* channel,i32* rssi) {
 if(!payload||!output||length<48) return 0;
 const u8* p=payload;u32 n=le16(p),status=le32(p+12);
 if((status&3)!=3||n<24||n>IWX_STATION_FRAME_MAX||n>length-48||(p[3]&0x40)) return 0;
 const u8* f=p+48;
 if((f[0]&3)||((f[0]>>2)&3)==1||(f[1]&3)==3) return 0;
 u32 header=24+((((f[0]>>2)&3)==2&&(f[0]&0x80))?2:0);
 if(n<header) return 0;
 // The hardware pad follows the MAC header, or the IV for hardware CCMP.
 // No hardware keys are installed, so decoded encrypted hardware frames
 // are rejected instead of passing plaintext as software protected data.
 if(status&0x800) return 0;
 if(p[3]&0x20) {
  if(n<header+2||n-2>capacity) return 0;
  copy(output,f,header);copy((u8*)output+header,f+header+2,n-header-2);n-=2;
 } else { if(n>capacity) return 0;copy(output,f,n); }
 if(channel) *channel=p[34];
 if(rssi) {u8 a=p[32],b=p[33];u8 e=(!a?b:!b?a:a<b?a:b);*rssi=e?-(i32)e:-128;}
 return n;
}

static bool legacy_rate(u8 rate) {
 static const u8 rates[]={2,4,11,22,12,18,24,36,48,72,96,108};
 for(u32 i=0;i<sizeof(rates);i++) if((rate&0x7f)==rates[i]) return true;
 return false;
}
static bool suite(const u8* p,u8 type) { return p[0]==0&&p[1]==0x0f&&p[2]==0xac&&p[3]==type; }
static bool rsn_psk_ccmp(const u8* p,u32 n) {
 if(n<8||le16(p)!=1||!suite(p+2,4)) return false;
 u32 off=6,count=le16(p+off);off+=2;
 if(!count||count>(n-off)/4) return false;
 bool ccmp=false;for(u32 i=0;i<count;i++) ccmp|=suite(p+off+4*i,4);off+=4*count;
 if(!ccmp||n-off<2) return false;
 count=le16(p+off);off+=2;if(!count||count>(n-off)/4) return false;
 bool psk=false;for(u32 i=0;i<count;i++) psk|=suite(p+off+4*i,2);off+=4*count;
 if(!psk) return false;
 if(n==off) return true;
 if(n-off<2||(le16(p+off)&(1u<<6))) return false;
 off+=2;
 if(n==off) return true;
 if(n-off<2) return false;
 count=le16(p+off);off+=2;if(count>(n-off)/16) return false;off+=16*count;
 return n==off||(n-off==4&&suite(p+off,6));
}

bool iwx_station_parse_beacon(const void* frame,u32 length,u8 channel,i32 rssi,iwx_scan_result_t* out) {
 if(!frame||!out||length<36||!channel) return false;
 const u8* p=frame;
 if((p[0]!=0x80&&p[0]!=0x50)||(p[1]&0x43)||!valid_mac(p+16)||!equal(p+10,p+16,6)) return false;
 u16 cap=le16(p+34);if(!(cap&1)||(cap&2)) return false;
 iwx_scan_result_t result;clear(&result,sizeof(result));
 copy(result.bssid,p+16,6);result.channel=channel;result.rssi_dbm=rssi;
 result.capability=cap;result.beacon_interval=le16(p+32);result.tsf=le64(p+24);
 result.security=(cap&0x10)?IWX_SECURITY_UNSUPPORTED:IWX_SECURITY_OPEN;
 bool have_ssid=false,have_rsn=false;
 for(u32 offset=36;offset<length;) {
  if(length-offset<2) return false;
  u8 id=p[offset],n=p[offset+1];offset+=2;if(n>length-offset) return false;
  const u8* ie=p+offset;
  if(id==0) {if(have_ssid||n>32) return false;have_ssid=true;result.ssid_length=n;copy(result.ssid,ie,n);}
  else if(id==1||id==50) {if(id==1&&n>8) return false;for(u32 i=0;i<n;i++) {
   if(!legacy_rate(ie[i])) {if(ie[i]&0x80) return false;continue;}
   bool duplicate=false;for(u32 j=0;j<result.rate_count;j++) if((result.rates[j]&0x7f)==(ie[i]&0x7f)) {result.rates[j]|=ie[i]&0x80;duplicate=true;}
   if(!duplicate&&result.rate_count<12) result.rates[result.rate_count++]=ie[i];
  }}
  else if(id==3) {if(n!=1||ie[0]!=channel) return false;}
  else if(id==5) {if(n<4||!ie[1]) return false;result.dtim_count=ie[0];result.dtim_period=ie[1];}
  else if(id==48) {if(have_rsn) return false;have_rsn=true;result.rsn[0]=48;result.rsn[1]=n;copy(result.rsn+2,ie,n);result.rsn_length=(u16)n+2;
   result.security=((cap&0x10)&&rsn_psk_ccmp(ie,n))?IWX_SECURITY_WPA2_PSK_CCMP:IWX_SECURITY_UNSUPPORTED;}
  offset+=n;
 }
 if(!have_ssid||!result.beacon_interval||!result.rate_count) return false;
 *out=result;return true;
}

static i32 update_regulatory(void) {
 // Ask firmware for its current/NVM country. Never choose a country based
 // on host locale. MCC response permissions are intersected with NVM.
 if(iwx200_cmd_version(1,0xc8)!=1) return -EOPNOTSUPP;
 i32 version=iwx200_notif_version(1,0xc8);if(version<4||version>7) return -EOPNOTSUPP;
 u8 req[28]={0x5a,0x5a,0x10};u8 response[20+110*4];
 i32 n=iwx200_command(1,0xc8,0,req,sizeof(req),response,sizeof(response));
 if(n<0) return n;
 if(n<20||le32(response)>1) return -EIO;
 u32 count=le32(response+16);if(!count||count>110||(u32)n!=20+count*4) return -EIO;
 clear(station.profiles,sizeof(station.profiles));
 for(u32 i=0;i<count&&i<station.profile_count;i++) station.profiles[i]=station.nvm_profiles[i]&le32(response+20+4*i);
 return 0;
}

i32 iwx_station_configure(const u8 mac[6],u32 tx,u32 rx,const u32* profiles,u32 count) {
 if(!valid_mac(mac)||!profiles||!count||count>110||!tx||!rx||(tx&~3u)||(rx&~3u)) return -EINVAL;
 if(station.scanning) return -EBUSY;
 if(count>51) count=51;
 clear(&station,sizeof(station));copy(station.mac,mac,6);station.tx_chains=tx;station.rx_chains=rx;station.profile_count=count;
 copy(station.nvm_profiles,profiles,count*4);station.configured=true;
 iwx200_set_notification_handler(iwx_station_notification);return 0;
}

i32 iwx_station_scan(void) {
 if(!station.configured) return -ENODEV;
 if(station.scanning) return -EBUSY;
 i32 cfg=iwx200_cmd_version(1,0xc),req=iwx200_cmd_version(1,0xd);
 if(cfg!=5||(req!=14&&req!=15)) return -EOPNOTSUPP;
 i32 err=update_regulatory();if(err<0) return err;
 struct iwx_station_scan_config config={0};config.tx_chains=station.tx_chains;config.rx_chains=station.rx_chains;
 err=iwx200_command(1,0xc,0,&config,sizeof(config),0,0);if(err<0) return err;
 struct iwx_station_scan_req request;clear(&request,sizeof(request));
 station.scan_uid=(station.scan_uid+0x100u)&0xffffff00u;
 request.uid=station.scan_uid;request.priority=6;
 // PASS_ALL, NTFY_ITER_COMPLETE, ADAPTIVE_DWELL, FORCE_PASSIVE.
 request.general.flags=(1u<<1)|(1u<<2)|(1u<<7)|(1u<<11);
 request.general.active_dwell[0]=request.general.active_dwell[1]=10;
 request.general.passive_dwell[0]=request.general.passive_dwell[1]=110;
 request.general.adwell_default_2g=2;request.general.adwell_default_5g=8;request.general.adwell_default_social=10;
 request.general.adwell_max_budget=300;request.general.scan_priority=6;
 request.channel_flags=1u<<5;request.n_aps_override[0]=10;request.n_aps_override[1]=2;
 for(u32 i=0;i<station.profile_count;i++) if(station.profiles[i]&1) {
  struct iwx_station_scan_channel* channel=&request.channels[request.channel_count++];
  channel->channel=channel_numbers[i];channel->band=i<14?1:0;channel->iterations=1;
 }
 if(!request.channel_count) return -ENODEV;
 request.schedules[0].iterations=1;
 // Valid probe template even for passive scan (firmware expects one).
 request.probe.mac_header.len=26;request.probe.buf[0]=0x40;
 for(u32 i=0;i<6;i++) request.probe.buf[4+i]=request.probe.buf[16+i]=255;
 copy(request.probe.buf+10,station.mac,6);
 static const u8 rates24[]={1,8,0x82,0x84,0x8b,0x96,12,18,24,36,50,4,48,72,96,108};
 static const u8 rates5[]={1,8,0x8c,18,0x98,36,0xb0,72,96,108};
 request.probe.band_data[0].offset=26;request.probe.band_data[0].len=sizeof(rates24);copy(request.probe.buf+26,rates24,sizeof(rates24));
 request.probe.band_data[1].offset=26+sizeof(rates24);request.probe.band_data[1].len=sizeof(rates5);copy(request.probe.buf+26+sizeof(rates24),rates5,sizeof(rates5));
 request.probe.common_data.offset=26+sizeof(rates24)+sizeof(rates5);
 station.result_count=0;station.scan_error=0;station.scanning=true;
 err=iwx200_command(1,0xd,0,&request,sizeof(request),0,0);
 if(err<0) {station.scanning=false;station.scan_error=err;return err;}return 0;
}
bool iwx_station_scanning(void) {return station.scanning;}
u32 iwx_station_scan_results(iwx_scan_result_t* out,u32 capacity) {
 if(!out) return 0;
 u32 n=station.result_count<capacity?station.result_count:capacity;copy(out,station.results,n*sizeof(*out));return n;
}
static void enqueue_rx(const u8* frame,u32 n);
static void session_notification(const u8* p,u32 n);
void iwx_station_notification(u8 group,u8 opcode,const u8* p,u32 length) {
 if(!p) return;
 if(group==3&&opcode==0xfb) {session_notification(p,length);return;}
 if(group==0&&opcode==0xc1) {
  u8 frame[IWX_STATION_FRAME_MAX],channel;i32 rssi;
  u32 n=iwx_station_decode_rx(p,length,frame,sizeof(frame),&channel,&rssi);
  iwx_scan_result_t result;
  if(!n) return;
  if(!station.scanning) {enqueue_rx(frame,n);return;}
  if(!iwx_station_parse_beacon(frame,n,channel,rssi,&result)) return;
  bool allowed=false;for(u32 i=0;i<station.profile_count;i++) if(channel_numbers[i]==channel&&(station.profiles[i]&1)) allowed=true;
  if(!allowed) return;
  result.device_time=le32(p+36);
  for(u32 i=0;i<station.result_count;i++) if(equal(station.results[i].bssid,result.bssid,6)) {station.results[i]=result;return;}
  if(station.result_count<IWX_STATION_RESULTS) station.results[station.result_count++]=result;
 } else if(station.scanning&&(group==0||group==1)&&(opcode==0xf||opcode==0xb5)) {
  if(length<16||le32(p)!=station.scan_uid) return;
  u8 status=p[opcode==0xf?6:5];
  station.scan_error=status==1?0:-EIO;station.scanning=false;
 }
}
// Station context lifecycle follows OpenBSD iwx_auth/iwx_run. A single
// nonaggregated queue (TID15) carries management and legacy data.
static struct {
 wifi_sta_t wifi;
 wpa_t wpa;
 iwx_scan_result_t ap;
 iwx_link_t link;
 bool phy,mac,binding,sta,session,session_started,secured,dirty;
 bool owner_active;
 i32 queue,error;
 u8 selected_bssid[6];
 bool selected;
 u8 raw[8][IWX_STATION_FRAME_MAX];u16 raw_lengths[8];u8 raw_read,raw_write;
 u8 ethernet[8][1514];u16 ethernet_lengths[8];u8 eth_read,eth_write;
 u64 rx_pn[2][17];u32 gtk_generation,ptk_generation;
 u64 next_retry,deadline;
} connection={.queue=-1};
static void consume_rx(void);
static void session_notification(const u8* p,u32 n) {
 if(n!=16||le32(p)!=0||le32(p+12)!=0) return;
 if(le32(p+8)==1) {if(le32(p+4)==1) connection.session_started=true;else connection.error=diag_fail(__LINE__,connection.link,-EIO);}
}
static void enqueue_rx(const u8* frame,u32 n) {
 u8 next=(connection.raw_write+1)&7;
 if(next==connection.raw_read||n>IWX_STATION_FRAME_MAX) return;
 copy(connection.raw[connection.raw_write],frame,n);connection.raw_lengths[connection.raw_write]=(u16)n;connection.raw_write=next;
}
static i32 command(u8 group,u8 op,const void* data,u32 length) {
 return iwx200_command(group,op,0,data,length,0,0);
}
static i32 status_command(u8 op,const void* data,u32 length,u32 expected) {
 u8 status[4];i32 n=iwx200_command(1,op,0,data,length,status,sizeof(status));
 if(n<0) return diag_fail(__LINE__,op,n);
 return n!=4||le32(status)!=expected?diag_fail(__LINE__,(u32)op<<24|(n==4?le32(status)&0xffffff:0xffffff),-EIO):0;
}
static i32 phy_context(u32 action) {
 struct iwx_station_phy_context cmd={0};cmd.action=action;
 cmd.band=connection.ap.channel<=14?1:0;cmd.channel=connection.ap.channel;
 // RX chain config: valid antennas, 1 idle chain, 1 active (MIMO) chain.
 u32 chains=(station.rx_chains<<1)|(1u<<10)|(1u<<12);
 // Firmware with RLC_CONFIG v2 (cc-a0-71) owns RX chains through RLC_CONFIG;
 // the PHY field is reserved there and non-zero values trip a UMAC assert.
 bool rlc=iwx200_cmd_version(5,8)==2;
 if(!rlc) cmd.rxchain_info=chains;
 i32 e;
 if(iwx200_has_capability(48)) { // cc-a0-71 sets ULTRA_HB_CHANNELS
  struct iwx_station_phy_context_uhb u={0};
  u.id_and_color=cmd.id_and_color;u.action=cmd.action;u.channel=cmd.channel;u.band=cmd.band;
  u.width=cmd.width;u.ctrl_pos=cmd.ctrl_pos;u.lmac_id=cmd.lmac_id;u.rxchain_info=cmd.rxchain_info;
  e=command(1,8,&u,sizeof(u));
 } else e=command(1,8,&cmd,sizeof(cmd));
 if(e<0||!rlc||action==3) return e;
 struct iwx_station_rlc_config rc={0};rc.phy_id=0;rc.rx_chain_info=chains;
 e=command(5,8,&rc,sizeof(rc));
 return e<0?diag_fail(__LINE__,0x58,e):e;
}
static i32 mac_context(u32 action,bool associated) {
 struct iwx_station_mac_context cmd={0};cmd.action=action;cmd.mac_type=5;
 copy(cmd.node,station.mac,6);copy(cmd.bssid,connection.ap.bssid,6);
 static const u8 cck[]={2,4,11,22},ofdm[]={12,18,24,36,48,72,96,108};
 for(u32 i=0;i<connection.ap.rate_count;i++) if(connection.ap.rates[i]&0x80) {
  u8 rate=connection.ap.rates[i]&0x7f;
  for(u32 j=0;j<4;j++) if(rate==cck[j]) cmd.cck_rates|=1u<<j;
  for(u32 j=0;j<8;j++) if(rate==ofdm[j]) cmd.ofdm_rates|=1u<<j;
 }
 // Firmware ACK-rate bitmaps always contain the lowest mandatory rate.
 if(connection.ap.channel<=14) cmd.cck_rates|=1;
 cmd.ofdm_rates|=1;
 cmd.short_preamble=(connection.ap.capability&(1u<<5))?0x20:0;
 cmd.short_slot=(connection.ap.capability&(1u<<10))?0x10:0;
 // Software CCMP: never let firmware decrypt before the host checks replay.
 cmd.filter_flags=(1u<<1)|(1u<<2)|(1u<<3)|(1u<<4)|(1u<<6);
 // Non-QoS legacy DCF. WMM is deliberately not advertised to the AP.
 for(u32 i=0;i<5;i++) {cmd.ac[i].cw_min=15;cmd.ac[i].cw_max=1023;cmd.ac[i].aifsn=2;cmd.ac[i].fifos_mask=(u8)(1u<<i);}
 cmd.sta.is_assoc=associated;cmd.sta.bi=connection.ap.beacon_interval;
 cmd.sta.dtim_interval=connection.ap.beacon_interval*(connection.ap.dtim_period?connection.ap.dtim_period:1);
 cmd.sta.listen_interval=10;cmd.sta.assoc_id=connection.wifi.aid;
 if(associated) {
  u32 offset=connection.ap.dtim_count*connection.ap.beacon_interval*1024;
  cmd.sta.dtim_time=connection.ap.device_time+offset;cmd.sta.dtim_tsf=connection.ap.tsf+offset;
  cmd.sta.assoc_beacon_arrive_time=connection.ap.device_time;
 }
 return command(1,0x28,&cmd,sizeof(cmd));
}
static i32 binding_context(u32 action) {
 struct iwx_station_binding cmd={0};cmd.action=action;cmd.macs[1]=cmd.macs[2]=0xffffffffu;
 return status_command(0x2b,&cmd,sizeof(cmd),0);
}
static i32 station_context(bool modify) {
 struct iwx_station_add_sta cmd={0};cmd.add_modify=modify;
 if(!modify) copy(cmd.addr,connection.ap.bssid,6);
 cmd.flags_mask=(3u<<26)|(3u<<28);cmd.assoc_id=connection.wifi.aid;
 return status_command(0x18,&cmd,sizeof(cmd),1);
}
static i32 session_context(u32 action) {
 struct iwx_station_session cmd={0};cmd.action=action;
 if(action==1) {connection.session_started=false;cmd.duration_tu=connection.ap.beacon_interval*9;}
 return command(3,5,&cmd,sizeof(cmd));
}
static bool raw_tx(const void* frame,u32 length) {
 u8 prefix[20];
 i32 v=iwx200_notif_version(1,0x1c);if(v<0) return false;
 if(connection.queue<0||!iwx_station_build_tx(prefix,sizeof(prefix),frame,length,(u8)station.tx_chains,connection.ap.channel<=14,v>6?2:1)) return false;
 // Pick the lowest basic legacy rate actually advertised by this AP.
 static const u8 rates[]={2,4,11,22,12,18,24,36,48,72,96,108};
 static const u8 plcp[]={10,20,55,110,13,15,5,7,9,11,1,3};
 u8 selected=0xff;u32 best=0xffffffffu;
 for(u32 i=0;i<connection.ap.rate_count;i++) if(connection.ap.rates[i]&0x80) {
  u8 rate=connection.ap.rates[i]&0x7f;
  for(u32 j=0;j<sizeof(rates);j++) if(rate==rates[j]&&rate<best) {selected=(u8)j;best=rate;}
 }
 if(selected==0xff) return false;
 u32 ant=station.tx_chains&(0u-station.tx_chains);
 u32 flags=(ant<<14)|(v>6?(selected<4?selected:((1u<<8)|(selected-4))):((selected<4?(1u<<9):0)|plcp[selected]));
 struct iwx_station_tx_cmd* tx=(void*)prefix;tx->rate_n_flags=flags;
 return iwx200_data_tx((u8)connection.queue,prefix,frame,length);
}
static i32 start_contexts(void) {
 if(iwx200_cmd_version(1,8)!=3||iwx200_cmd_version(1,0x28)!=5||iwx200_cmd_version(1,0x2b)!=2||
    iwx200_cmd_version(1,0x18)!=12||iwx200_cmd_version(1,0x19)!=2||iwx200_cmd_version(3,5)!=1||
    iwx200_cmd_version(1,0x1c)!=9||iwx200_notif_version(3,0xfb)!=2) return diag_fail(__LINE__,0,-EOPNOTSUPP);
 // extra = step number: 1 phy, 2 mac, 3 binding, 4 station, 5 tx queue, 6 session
 i32 e=phy_context(1);if(e<0) return diag_fail(__LINE__,1,e);connection.phy=true;
 e=mac_context(1,false);if(e<0) return diag_fail(__LINE__,2,e);connection.mac=true;
 e=binding_context(1);if(e<0) return diag_fail(__LINE__,3,e);connection.binding=true;
 e=station_context(false);if(e<0) return diag_fail(__LINE__,4,e);connection.sta=true;
 e=iwx200_data_queue_enable(0,15);if(e<0) return diag_fail(__LINE__,5,e);connection.queue=e;
 e=session_context(1);if(e<0) return diag_fail(__LINE__,6,e);connection.session=true;
 return 0;
}
void iwx_station_disconnect(void) {
 // Disable the software link before any firmware work, and wipe the PMK.
 connection.link=IWX_LINK_DOWN;wpa_reset(&connection.wpa);
 i32 error=0,e;
 if(connection.queue>=0) {e=iwx200_data_queue_disable((u8)connection.queue);if(e<0) error=e;connection.queue=-1;}
 if(connection.session) {e=session_context(3);if(e<0) error=e;else connection.session=false;}
 if(connection.sta) {u8 req[4]={0};e=command(1,0x19,req,sizeof(req));if(e<0) error=e;else connection.sta=false;}
 if(connection.binding) {e=binding_context(3);if(e<0) error=e;else connection.binding=false;}
 if(connection.mac) {e=mac_context(3,false);if(e<0) error=e;else connection.mac=false;}
 if(connection.phy) {e=phy_context(3);if(e<0) error=e;else connection.phy=false;}
 connection.dirty=error<0;connection.error=error;
 wifi_sta_init(&connection.wifi,station.mac);
 connection.raw_read=connection.raw_write=connection.eth_read=connection.eth_write=0;
 clear(connection.rx_pn,sizeof(connection.rx_pn));connection.gtk_generation=connection.ptk_generation=0;
}
static u32 ethernet_frame(u8* out,const u8* eth,u32 n) {
 if(n<14||n>1514||!equal(eth+6,station.mac,6)) return 0;
 clear(out,24);out[0]=8;out[1]=1;copy(out+4,connection.ap.bssid,6);copy(out+10,station.mac,6);copy(out+16,eth,6);
 u16 seq=connection.wifi.sequence++&0xfff;out[22]=(u8)(seq<<4);out[23]=(u8)(seq>>4);
 out[24]=out[25]=0xaa;out[26]=3;out[27]=out[28]=out[29]=0;copy(out+30,eth+12,n-12);return n+18;
}
static void aad_for_frame(const u8* f,u8* aad,bool qos) {
 aad[0]=f[0]&0x8f;aad[1]=f[1]&0xc7;copy(aad+2,f+4,18);aad[20]=f[22]&15;aad[21]=0;
 if(qos) {aad[22]=f[24]&15;aad[23]=0;}
}
static bool secure_tx(u8* frame,u32* n) {
 if(!wpa_completed(&connection.wpa)||*n<24||*n+16>IWX_STATION_FRAME_MAX||connection.wpa.tx_pn>=0xffffffffffffULL) return false;
 static u8 encrypted[IWX_STATION_FRAME_MAX]; // static: keeps the RX path inside KSTACK_SIZE
 u8 pn[6],aad[22];u64 number=++connection.wpa.tx_pn;
 for(u32 i=0;i<6;i++) pn[i]=(u8)(number>>(8*i));
 frame[1]|=0x40;aad_for_frame(frame,aad,false);u32 encrypted_length=sizeof(encrypted);
 if(!wifi_ccmp_encrypt(connection.wpa.ptk+32,pn,station.mac,aad,22,frame+24,*n-24,encrypted,&encrypted_length)) return false;
 frame[24]=pn[0];frame[25]=pn[1];frame[26]=0;frame[27]=0x20;copy(frame+28,pn+2,4);
 copy(frame+32,encrypted,encrypted_length);*n=32+encrypted_length;return true;
}
static bool secure_rx(u8* frame,u32* n) {
 wifi_frame_t parsed;if(!wifi_parse(frame,*n,&parsed)||!parsed.protected) return false;
 bool qos=parsed.subtype&8;u32 header=(u32)(parsed.body-frame);
 if(header<24||parsed.body_length<16) return false;
 const u8* iv=frame+header;
 if(iv[2]||(iv[3]&0x3f)!=0x20) return false;
 u8 keyid=iv[3]>>6;bool group=frame[4]&1;
 if(group&&(!connection.wpa.gtk_ok||keyid!=connection.wpa.gtk_index)) return false;
 if(!group&&keyid) return false;
 u8 pn[6]={iv[0],iv[1],iv[4],iv[5],iv[6],iv[7]};u64 number=0;
 for(u32 i=0;i<6;i++) number|=(u64)pn[i]<<(8*i);
 u8 tid=qos?(frame[24]&15):16;
 if(number<=connection.rx_pn[group][tid]) return false;
 static u8 plain[IWX_STATION_FRAME_MAX];u8 aad[24];aad_for_frame(frame,aad,qos);u32 plain_length=sizeof(plain);
 const u8* key=group?connection.wpa.gtk:connection.wpa.ptk+32;
 if(!wifi_ccmp_decrypt(key,pn,frame+10,aad,qos?24:22,iv+8,*n-header-8,plain,&plain_length)) return false;
 connection.rx_pn[group][tid]=number;copy(frame+header,plain,plain_length);*n=header+plain_length;frame[1]&=~0x40;return true;
}
static bool eapol_reply(const u8* p,u32 n) {
 static u8 eth[1514],frame[IWX_STATION_FRAME_MAX];if(n>1500) return false;
 copy(eth,connection.ap.bssid,6);copy(eth+6,station.mac,6);eth[12]=0x88;eth[13]=0x8e;copy(eth+14,p,n);
 u32 length=ethernet_frame(frame,eth,14+n);
 if(length&&n>=7&&!(p[6]&WPA_KEYINFO_KEY)&&connection.secured&&connection.link==IWX_LINK_UP&&!secure_tx(frame,&length)) return false;
 return length&&raw_tx(frame,length);
}
static void consume_rx(void) {
 while(connection.raw_read!=connection.raw_write) {
  static u8 frame[IWX_STATION_FRAME_MAX];u32 n=connection.raw_lengths[connection.raw_read];
  copy(frame,connection.raw[connection.raw_read],n);connection.raw_read=(connection.raw_read+1)&7;
  wifi_frame_t parsed;if(!wifi_parse(frame,n,&parsed)||!equal(parsed.addr2,connection.ap.bssid,6)) continue;
  if(parsed.type==0) {
   if(parsed.protected) continue;
   if((parsed.subtype==10||parsed.subtype==12)&&parsed.body_length>=2&&equal(parsed.addr1,station.mac,6)&&equal(parsed.addr3,connection.ap.bssid,6)) {connection.link=IWX_LINK_DOWN;connection.error=diag_fail(__LINE__,(u32)parsed.subtype<<16|parsed.body[0]|parsed.body[1]<<8,-EIO);continue;}
   wifi_receive(&connection.wifi,frame,n);
   if(connection.wifi.state==WIFI_IDLE&&connection.link!=IWX_LINK_DOWN) {connection.link=IWX_LINK_DOWN;connection.error=diag_fail(__LINE__,connection.wifi.last_status,-EIO);}
   continue;
  }
  if(connection.wifi.state!=WIFI_ASSOCIATED||parsed.type!=2||parsed.to_ds||!parsed.from_ds||
     (!equal(parsed.addr1,station.mac,6)&&!(parsed.addr1[0]&1))) continue;
  bool encrypted=parsed.protected;
  if(encrypted&&(!connection.secured||!wpa_completed(&connection.wpa)||!secure_rx(frame,&n))) continue;
  static u8 eth[1514];u32 length=wifi_to_ethernet(&connection.wifi,frame,n,eth,sizeof(eth));
  if(length<14) continue;
  if(eth[12]==0x88&&eth[13]==0x8e) {
   if(!connection.secured||!equal(eth,station.mac,6)||!equal(eth+6,connection.ap.bssid,6)) continue;
   u8 reply[512];u32 reply_length=0;
   bool was_complete=wpa_completed(&connection.wpa);
   wpa_receive(&connection.wpa,eth+14,length-14,reply,sizeof(reply),&reply_length);
   // Pairwise rekey requires an old/new outer-CCMP key transition that this
   // baseline does not implement. Stop and wipe keys on a fresh M1. Stable
   // M1/M3 retransmissions keep completion and never reinstall/reset keys.
   if(was_complete&&!wpa_completed(&connection.wpa)) {
    connection.link=IWX_LINK_DOWN;connection.error=-EOPNOTSUPP;
    wpa_reset(&connection.wpa);continue;
   }
   if(reply_length&&!eapol_reply(reply,reply_length)) {connection.link=IWX_LINK_DOWN;connection.error=diag_fail(__LINE__,connection.link,-EIO);continue;}
   if(wpa_completed(&connection.wpa)&&reply_length) {
    if(connection.wpa.ptk_generation!=connection.ptk_generation) {
     clear(connection.rx_pn[0],sizeof(connection.rx_pn[0]));
     connection.ptk_generation=connection.wpa.ptk_generation;
    }
    if(connection.wpa.gtk_generation!=connection.gtk_generation) {
     for(u32 i=0;i<17;i++) connection.rx_pn[1][i]=connection.wpa.gtk_rsc;
     connection.gtk_generation=connection.wpa.gtk_generation;
    }
    connection.link=IWX_LINK_UP;
   }
   continue;
  }
  if(connection.link!=IWX_LINK_UP||(connection.secured&&!encrypted)) continue;
  u8 next=(connection.eth_write+1)&7;if(next==connection.eth_read) continue;
  copy(connection.ethernet[connection.eth_write],eth,length);connection.ethernet_lengths[connection.eth_write]=(u16)length;connection.eth_write=next;
 }
}
static void poll_internal(void) {
 iwx200_poll();
 if(!iwx200_online()) {
  connection.link=IWX_LINK_DOWN;connection.dirty=true;connection.error=diag_fail(__LINE__,connection.link,-EIO);
  wpa_reset(&connection.wpa);connection.raw_read=connection.raw_write;
  connection.eth_read=connection.eth_write;return;
 }
 consume_rx();
 if(connection.link==IWX_LINK_DOWN&&connection.error) {
  wpa_reset(&connection.wpa);connection.eth_read=connection.eth_write;
 }
}
void iwx_station_poll(void) {
 if(connection.owner_active) return;
 connection.owner_active=true;poll_internal();
 if(!connection.dirty&&connection.link==IWX_LINK_DOWN&&(connection.phy||connection.mac)) iwx_station_disconnect();
 connection.owner_active=false;
}
static i32 prepare_station(void) {
 if(station.configured) return 0;
 if(!iwx200_online()) return -ENODEV;
 i32 e=iwx200_run_init();if(e<0) return e;
 iwx200_nvm_t nvm;if(!iwx200_get_nvm(&nvm)) return -ENODEV;
 e=iwx200_configure_radio();if(e<0) return e;
 u8 mac[6];iwx200_nvm_mac(mac);
 return iwx_station_configure(mac,nvm.tx_chains,nvm.rx_chains,nvm.channel_profiles,nvm.channel_count);
}
static i32 scan_and_wait(void) {
 i32 e=iwx_station_scan();if(e<0) return e;
 u64 deadline=time_counter_ns()+12000000000ULL;
 while(station.scanning) {
  iwx200_poll();if(time_counter_ns()>=deadline) {station.scanning=false;station.scan_error=-ETIMEDOUT;return -ETIMEDOUT;}
 }
 return station.scan_error;
}
i32 iwx_station_connect(const void* ssid,u32 length,const char* passphrase) {
 diag=(iwx_station_diag_t){0};
 if(!ssid||!length||length>32) return -EINVAL;
 if(connection.dirty) return diag_fail(__LINE__,0,-EIO);
 i32 e=prepare_station();if(e<0) return e;
 if(connection.link!=IWX_LINK_DOWN||connection.phy||connection.mac) iwx_station_disconnect();
 if(connection.dirty) return diag_fail(__LINE__,0,-EIO);
 e=scan_and_wait();if(e<0) return e;
 bool found=false;
 for(u32 i=0;i<station.result_count;i++) {
  iwx_scan_result_t* ap=&station.results[i];
  if(ap->ssid_length!=length||!equal(ap->ssid,ssid,length)||(connection.selected&&!equal(ap->bssid,connection.selected_bssid,6))) continue;
  if(ap->security!=(passphrase?IWX_SECURITY_WPA2_PSK_CCMP:IWX_SECURITY_OPEN)) continue;
  bool allowed=false;for(u32 j=0;j<station.profile_count;j++) if(channel_numbers[j]==ap->channel&&(station.profiles[j]&9)==9&&!(station.profiles[j]&(1u<<4))) allowed=true;
  if(!allowed) continue; // passive/DFS-only channels cannot originate auth
  if(!found||ap->rssi_dbm>connection.ap.rssi_dbm) {connection.ap=*ap;found=true;}
 }
 if(!found) return -2;
 connection.secured=connection.ap.security==IWX_SECURITY_WPA2_PSK_CCMP;
 if(connection.secured) {
  if(!passphrase) return -EINVAL;
  u32 n=0;while(n<64&&passphrase[n]) n++;if(n<8||n>63) return -EINVAL;
  wpa_derive_pmk(&connection.wpa,passphrase,ssid,length);
  wpa_start(&connection.wpa,connection.ap.bssid,station.mac);
  if(!wpa_set_ap_rsn(&connection.wpa,connection.ap.rsn,connection.ap.rsn_length)) {wpa_reset(&connection.wpa);return -EOPNOTSUPP;}
 }
 wifi_sta_init(&connection.wifi,station.mac);
 if(!wifi_sta_join(&connection.wifi,connection.ap.bssid,ssid,length)) return -EINVAL;
 wifi_sta_set_wpa2(&connection.wifi,connection.secured);
 if(!wifi_sta_set_rates(&connection.wifi,connection.ap.rates,connection.ap.rate_count)) return -EINVAL;
 connection.error=0;connection.link=IWX_LINK_ASSOCIATING;
 e=start_contexts();if(e<0) goto fail;
 // Firmware session protection must really start before sending auth.
 connection.deadline=time_counter_ns()+15000000000ULL;connection.next_retry=0;
 u64 session_deadline=time_counter_ns()+1000000000ULL;
 bool configured_assoc=false;
 while(connection.link!=IWX_LINK_UP) {
  poll_internal();
  if(connection.error) {e=connection.error;goto fail;}
  u64 now=time_counter_ns();if(now>=connection.deadline) {e=diag_fail(__LINE__,connection.wifi.state,-ETIMEDOUT);goto fail;}
  if(connection.wifi.state==WIFI_ASSOCIATED&&!configured_assoc) {
   if(!connection.wifi.aid||connection.wifi.aid>2007) {e=diag_fail(__LINE__,connection.wifi.state,-EIO);goto fail;}
   e=station_context(true);if(e<0) goto fail;
   e=mac_context(2,true);if(e<0) goto fail;
   u8 multicast[12]={1,0,0,1};copy(multicast+4,connection.ap.bssid,6);
   if(iwx200_cmd_version(1,0xd0)!=1) {e=-EOPNOTSUPP;goto fail;}
   e=command(1,0xd0,multicast,sizeof(multicast));if(e<0) goto fail;
   configured_assoc=true;connection.link=connection.secured?IWX_LINK_HANDSHAKE:IWX_LINK_UP;
  }
  if(!connection.session_started) {if(now>=session_deadline) {e=diag_fail(__LINE__,connection.wifi.state,-ETIMEDOUT);goto fail;}continue;}
  if(!configured_assoc) {
   u32 tx=wifi_next_tx(&connection.wifi);
   if(tx) {if(!raw_tx(connection.wifi.last_tx,tx)) {e=diag_fail(__LINE__,connection.wifi.state,-EIO);goto fail;}connection.next_retry=now+500000000ULL;}
   else if(now>=connection.next_retry&&connection.wifi.last_tx_length) {
    connection.wifi.last_tx[1]|=8;
    if(!raw_tx(connection.wifi.last_tx,connection.wifi.last_tx_length)) {e=diag_fail(__LINE__,connection.wifi.state,-EIO);goto fail;}
    connection.next_retry=now+500000000ULL;
   }
  }
 }
 if(connection.session) {e=session_context(3);if(e<0) goto fail;connection.session=false;}
 return 0;
fail:
 diag_fail(__LINE__,connection.wifi.state,e);diag.wifi_state=connection.wifi.state;diag.link=connection.link;diag.last_status=connection.wifi.last_status;
 iwx_station_disconnect();return e;
}
bool iwx_station_send(const void* ethernet,u32 length) {
 if(connection.link!=IWX_LINK_UP||(connection.secured&&!wpa_completed(&connection.wpa))||!ethernet||length<14||length>1514) return false;
 u8 frame[IWX_STATION_FRAME_MAX];u32 n=ethernet_frame(frame,ethernet,length);
 if(!n||(connection.secured&&!secure_tx(frame,&n))) return false;
 return raw_tx(frame,n);
}
u32 iwx_station_receive(void* ethernet,u32 capacity) {
 if(connection.link!=IWX_LINK_UP||!ethernet||connection.eth_read==connection.eth_write) return 0;
 u32 length=connection.ethernet_lengths[connection.eth_read];if(length>capacity) return 0;
 copy(ethernet,connection.ethernet[connection.eth_read],length);connection.eth_read=(connection.eth_read+1)&7;return length;
}
// Public control-plane wrappers serialize long operations against net_poll.
iwx_link_t iwx200_link(void) {return iwx200_online()?connection.link:IWX_LINK_DOWN;}
i32 iwx200_scan(iwx_scan_result* out,u32 capacity) {
 if(!out||!capacity) return -EINVAL;
 if(connection.owner_active||connection.link!=IWX_LINK_DOWN) return -EBUSY;
 connection.owner_active=true;i32 e=prepare_station();if(e>=0) e=scan_and_wait();
 if(e>=0) {u32 n=station.result_count<capacity?station.result_count:capacity;
  for(u32 i=0;i<n;i++) {clear(out+i,sizeof(*out));copy(out[i].bssid,station.results[i].bssid,6);copy(out[i].ssid,station.results[i].ssid,station.results[i].ssid_length);
   out[i].ssid_length=station.results[i].ssid_length;out[i].channel=station.results[i].channel;out[i].signal_dbm=(i16)station.results[i].rssi_dbm;out[i].privacy=(station.results[i].capability&0x10)!=0;}
  e=(i32)n;
 }
 connection.owner_active=false;return e;
}
i32 iwx200_connect(const void* ssid,u32 length,const char* passphrase) {
 if(connection.owner_active) return -EBUSY;
 connection.owner_active=true;i32 e=iwx_station_connect(ssid,length,passphrase);connection.owner_active=false;return e;
}
void iwx200_disconnect(void) {
 if(connection.owner_active) return;
 connection.owner_active=true;iwx_station_disconnect();connection.owner_active=false;
}
bool iwx200_set_bssid(const u8 bssid[6]) {
 if(!valid_mac(bssid)||connection.owner_active||connection.link!=IWX_LINK_DOWN) return false;
 copy(connection.selected_bssid,bssid,6);connection.selected=true;return true;
}
bool iwx200_send(const void* frame,u32 length) {
 if(connection.owner_active) return false;
 connection.owner_active=true;poll_internal();bool queued=iwx_station_send(frame,length);connection.owner_active=false;return queued;
}
u32 iwx200_receive(void* frame,u32 capacity) {
 if(connection.owner_active) return 0;
 connection.owner_active=true;poll_internal();u32 n=iwx_station_receive(frame,capacity);connection.owner_active=false;return n;
}
