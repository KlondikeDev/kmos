// This file has been generated in whole or in part by a Large Language Model
#include <wifi_control.h>
#include <wifi_ioctl.h>
#include <iwx200.h>
#include <proc.h>
#include <errno.h>
#include <io.h>
#include <memcpy.h>
#include <memset.h>
static struct kwifi_join pending_join;
static struct kwifi_result result;
static u32 next_job, pending_request;
static bool running;
static void wipe(void* p,u32 n) { volatile u8* b=p;while(n--)*b++=0; }
u32 wifi_control_size(u32 req) {
    if(req==KWIFI_JOIN)return sizeof(struct kwifi_join);
    if(req==KWIFI_RESULT)return sizeof(struct kwifi_result);
    return 0;
}
i64 wifi_control_ioctl(u32 req,void* arg) {
    if(proc_current()->euid)return -EACCES;
    if(req!=KWIFI_SCAN && req!=KWIFI_JOIN && req!=KWIFI_DISCONNECT && req!=KWIFI_RESULT)return -ENOTTY;
    u64 q=irq_save();
    if(req==KWIFI_RESULT){
        struct kwifi_result* out=arg;
        if(!result.job || out->job!=result.job){irq_restore(q);return -ENOENT;}
        memcpy(out,&result,sizeof(result));irq_restore(q);return 0;
    }
    if(pending_request || running){irq_restore(q);return -EBUSY;}
    if(req==KWIFI_JOIN){
        struct kwifi_join* in=arg;
        if(!in->ssid_length || in->ssid_length>32 ||
           (in->passphrase_length && (in->passphrase_length<8 || in->passphrase_length>63))){
            irq_restore(q);return -EINVAL;
        }
        for(u32 i=0;i<in->passphrase_length;i++)if(in->passphrase[i]<32 || in->passphrase[i]>126){irq_restore(q);return -EINVAL;}
        memset(&pending_join,0,sizeof(pending_join));
        pending_join.ssid_length=in->ssid_length;
        memcpy(pending_join.ssid,in->ssid,in->ssid_length);
        memcpy(pending_join.passphrase,in->passphrase,in->passphrase_length);
    }
    memset(&result,0,sizeof(result));
    if(!++next_job)++next_job;
    result.job=next_job;pending_request=req;
    irq_restore(q);return next_job;
}
void wifi_control_poll(void) {
    u64 q=irq_save();
    if(running || !pending_request){irq_restore(q);return;}
    running=true;u32 req=pending_request;pending_request=0;irq_restore(q);
    i32 error=0;u32 count=0;
    // Only the owning PID 0 operation consumes scan results. Publication
    // follows completion; clients ignore the array until complete is set.
    if(req==KWIFI_SCAN){
        iwx_scan_result networks[KWIFI_MAX_RESULTS];
        i32 n=iwx200_scan(networks,KWIFI_MAX_RESULTS);
        if(n<0)error=n;
        else {
            count=(u32)n;if(count>KWIFI_MAX_RESULTS)count=KWIFI_MAX_RESULTS;
            for(u32 i=0;i<count;i++){
                struct kwifi_network* out=&result.networks[i];
                memcpy(out->bssid,networks[i].bssid,6);memcpy(out->ssid,networks[i].ssid,32);
                out->ssid_length=networks[i].ssid_length;out->channel=networks[i].channel;
                out->signal_dbm=networks[i].signal_dbm;out->privacy=networks[i].privacy;
            }
        }
    }else if(req==KWIFI_JOIN){
        error=iwx200_connect(pending_join.ssid,pending_join.ssid_length,
                            pending_join.passphrase[0]?pending_join.passphrase:0);
        wipe(&pending_join,sizeof(pending_join));
    }else iwx200_disconnect();
    q=irq_save();result.error=error;result.count=count;result.complete=1;running=false;irq_restore(q);
}
