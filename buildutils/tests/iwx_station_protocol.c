// This file has been generated in whole or in part by a Large Language Model
// Independent byte fixtures taken from OpenBSD if_iwxreg.h field offsets.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <iwx200_station.h>
#include <iwx_station_hw.h>
#include <iwx200.h>
static u8 command[4096];
static u32 command_length;
static u32 command_opcode;
static void (*handler)(u8,u8,const u8*,u32);
bool iwx200_has_capability(u32 bit) { return bit==48; }
i32 iwx200_cmd_version(u8 group,u8 op) { (void)group; return op==0xc?5:op==0xd?15:op==0xc8?1:-1; }
i32 iwx200_notif_version(u8 group,u8 op) { (void)group; return op==0xc8?5:-1; }
void iwx200_set_notification_handler(void (*cb)(u8,u8,const u8*,u32)) { handler=cb; }
void iwx200_poll(void) { }
i32 iwx200_command(u32 group,u32 op,u32 version,const void* p,u32 n,void* r,u32 cap) {
 (void)group; (void)version; assert(n<=sizeof(command)); memcpy(command,p,n); command_length=n;command_opcode=op;
 if(op==0xc8) { assert(cap>=224); memset(r,0,224); ((u8*)r)[16]=51; ((u8*)r)[20]=9; ((u8*)r)[24]=9; return 224; }
 return 0;
}
static const u8 beacon[] = {
 0x80,0,0,0,255,255,255,255,255,255,2,3,4,5,6,7,2,3,4,5,6,7,0,0,
 1,2,3,4,5,6,7,8,100,0,1,0,
 0,4,'t','e','s','t',1,4,0x82,0x84,0x8b,0x96,3,1,1,5,4,0,2,0,0
};
int main(void) {
 u8 tx[20],rx[128],out[128];u8 channel=0;i32 rssi=0;
 const u8 expected[20]={24,0,0,12,7,0,0,0,0,0,0,0,0,0,0,0,10,0x42,0,0};
 u8 frame[24]={0xb0};
 assert(iwx_station_build_tx(tx,20,frame,24,1,true,1));
 assert(!memcmp(tx,expected,20));
 assert(!iwx_station_build_tx(tx,19,frame,24,1,true,1));
 assert(!iwx_station_build_tx(tx,20,frame,23,1,true,1));
 memset(rx,0,sizeof(rx));rx[0]=24;rx[12]=3;rx[32]=61;rx[33]=45;rx[34]=11;memcpy(rx+48,frame,24);
 assert(iwx_station_decode_rx(rx,72,out,sizeof(out),&channel,&rssi)==24);
 assert(channel==11&&rssi==-45&&!memcmp(out,frame,24));
 assert(!iwx_station_decode_rx(rx,71,out,sizeof(out),&channel,&rssi));
 rx[12]=1;assert(!iwx_station_decode_rx(rx,72,out,sizeof(out),&channel,&rssi));rx[12]=3;
 rx[3]=0x40;assert(!iwx_station_decode_rx(rx,72,out,sizeof(out),&channel,&rssi));rx[3]=0;
 iwx_scan_result_t result;
 assert(iwx_station_parse_beacon(beacon,sizeof(beacon),1,-55,&result));
 assert(result.ssid_length==4&&!memcmp(result.ssid,"test",4)&&result.security==IWX_SECURITY_OPEN&&result.dtim_period==2);
 for(u32 n=0;n<36;n++) assert(!iwx_station_parse_beacon(beacon,n,1,-55,&result));
 u8 malformed[sizeof(beacon)+1];memcpy(malformed,beacon,sizeof(beacon));malformed[sizeof(beacon)]=48;
 assert(!iwx_station_parse_beacon(malformed,sizeof(malformed),1,-55,&result));
 u32 profiles[51]={9,9};u8 mac[6]={2,0,0,0,0,1};
 assert(iwx_station_configure(mac,3,3,profiles,51)==0);
 assert(iwx_station_scan()==0&&handler&&command_opcode==0xd&&command_length==1940);
 // Header, flags, channel count, first channels, one scan iteration and probe offsets.
 assert(command[4]==6&&command[8]==0x86&&command[9]==8&&command[45]==2);
 assert(command[52]==1&&command[53]==1&&command[54]==1&&command[60]==2);
 assert(command[586]==1&&command[598]==26&&command[616]==0x40);
 assert(iwx_station_scanning());
 u8 complete[16]={0,1};complete[6]=1;handler(0,0xf,complete,16);
 assert(!iwx_station_scanning());
 puts("iwx station wire protocol: ok");
}
