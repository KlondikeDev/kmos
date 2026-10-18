// This file has been generated in whole or in part by a Large Language Model
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <proc.h>
#include <iwx200.h>
#include <wifi_control.h>
#include <wifi_ioctl.h>
static process_t process;
static unsigned scans,joins,disconnects;
process_t* proc_current(void){return &process;}
i32 iwx200_scan(iwx_scan_result* out,u32 cap){scans++;assert(cap==32);memset(out,0,sizeof(*out));out->ssid_length=3;memcpy(out->ssid,"AP1",3);out->channel=6;out->signal_dbm=-42;return 1;}
i32 iwx200_connect(const void* ssid,u32 n,const char* pass){joins++;assert(n==3 && !memcmp(ssid,"AP1",3));assert(pass && !strcmp(pass,"password"));return -110;}
void iwx200_disconnect(void){disconnects++;}
int main(void){
 struct kwifi_join in={.ssid_length=3,.passphrase_length=8,.ssid="AP1",.passphrase="password"};
 struct kwifi_result out={0};
 process.euid=1000;assert(wifi_control_ioctl(KWIFI_JOIN,&in)==-13);wifi_control_poll();assert(!joins);
 process.euid=0;assert(wifi_control_ioctl(0,0)==-25);
 assert(wifi_control_ioctl(KWIFI_RESULT,&out)==-2);
 in.ssid_length=33;assert(wifi_control_ioctl(KWIFI_JOIN,&in)==-22);in.ssid_length=3;
 in.passphrase_length=7;assert(wifi_control_ioctl(KWIFI_JOIN,&in)==-22);in.passphrase_length=64;assert(wifi_control_ioctl(KWIFI_JOIN,&in)==-22);in.passphrase_length=8;
 int job=wifi_control_ioctl(KWIFI_JOIN,&in);assert(job>0 && !joins);
 out.job=job;assert(wifi_control_ioctl(KWIFI_RESULT,&out)==0 && !out.complete);
 assert(wifi_control_ioctl(KWIFI_SCAN,0)==-16);
 wifi_control_poll();assert(joins==1);assert(wifi_control_ioctl(KWIFI_RESULT,&out)==0 && out.complete && out.error==-110);
 out.job=job+100;assert(wifi_control_ioctl(KWIFI_RESULT,&out)==-2);
 out.job=wifi_control_ioctl(KWIFI_SCAN,0);assert(out.job!=(unsigned)job);wifi_control_poll();
 assert(wifi_control_ioctl(KWIFI_RESULT,&out)==0 && out.complete && !out.error && out.count==1 && out.networks[0].signal_dbm==-42 && out.networks[0].channel==6 && !memcmp(out.networks[0].ssid,"AP1",3));
 wifi_control_poll();assert(scans==1);
 assert(wifi_control_ioctl(KWIFI_DISCONNECT,0)>0);wifi_control_poll();assert(disconnects==1);
 assert(wifi_control_size(KWIFI_JOIN)==sizeof(in) && wifi_control_size(KWIFI_RESULT)==sizeof(out));
 puts("wifi control: root authorization, validation, deferred jobs, busy/result identity and errors PASS");return 0;
}
