// This file has been generated in whole or in part by a Large Language Model
#include "kunix.h"
#include "../include/wifi_ioctl.h"
static struct kwifi_join join_request;
static int terminal_mode=-1;
static void wipe(void) { volatile char* p=(void*)&join_request;for(size_t i=0;i<sizeof(join_request);i++)p[i]=0; }
static void restore(void) { if(terminal_mode>=0){ioctl(0,TIOCSMODE,terminal_mode);terminal_mode=-1;} }
static void interrupted(int sig) { restore();wipe();_exit(128+sig); }
static void safe_ssid(const unsigned char* s,unsigned int n) {
    if(n>32)n=32;
    for(unsigned int i=0;i<n;i++)if(s[i]>=32 && s[i]<=126)printf("%c",s[i]);else printf("\\x%02x",s[i]);
}
static int status(void) {
    int fd=open("/proc/wifi",O_RDONLY);if(fd<0){perror("wifi status");return 1;}
    char b[512];ssize_t n;while((n=read(fd,b,sizeof(b)))>0)write(1,b,n);close(fd);return n<0;
}
static int password(void) {
    terminal_mode=ioctl(0,TIOCGMODE,0);
    if(terminal_mode<0){dprintf(2,"wifi: enter the passphrase from a terminal\n");return -1;}
    signal(SIGINT,interrupted);signal(SIGTERM,interrupted);signal(SIGHUP,interrupted);
    signal(SIGQUIT,interrupted);signal(SIGTSTP,SIG_IGN);
    printf("Passphrase: ");
    if(ioctl(0,TIOCSMODE,terminal_mode&~TTY_MODE_ECHO)<0){restore();return -1;}
    unsigned int n=0;int invalid=0;char c=0;ssize_t got;
    while((got=read(0,&c,1))==1 && c!='\n'){
        if(n<63)join_request.passphrase[n++]=c;else invalid=1;
    }
    restore();printf("\n");
    if(got!=1 || c!='\n'){dprintf(2,"wifi: passphrase cancelled\n");return -1;}
    if(invalid || n<8){dprintf(2,"wifi: passphrase must contain 8 to 63 bytes\n");return -1;}
    join_request.passphrase_length=n;return 0;
}
int main(int argc,char** argv) {
    if(argc==1 || (argc==2 && !strcmp(argv[1],"status")))return status();
    unsigned int request;
    if(argc==2 && !strcmp(argv[1],"scan"))request=KWIFI_SCAN;
    else if(argc==2 && !strcmp(argv[1],"disconnect"))request=KWIFI_DISCONNECT;
    else if((argc==3 || (argc==4 && !strcmp(argv[3],"--open"))) && !strcmp(argv[1],"join")){
        request=KWIFI_JOIN;size_t n=strlen(argv[2]);
        if(!n || n>32){dprintf(2,"wifi: SSID must contain 1 to 32 bytes\n");return 1;}
        join_request.ssid_length=n;memcpy(join_request.ssid,argv[2],n);
    }else {
        dprintf(2,"usage: wifi [status|scan|join SSID [--open]|disconnect]\n");return 1;
    }
    int fd=open("/dev/wifi0",O_RDWR);
    if(fd<0){perror("wifi (run as root)");wipe();return 1;}
    if(request==KWIFI_JOIN && argc==3 && password()<0){wipe();close(fd);return 1;}
    int job=ioctl(fd,request,request==KWIFI_JOIN?(unsigned long)&join_request:0);
    wipe();
    if(job<0){perror("wifi");close(fd);return 1;}
    struct kwifi_result result={0};result.job=job;
    // A bounded driver deadline completes jobs. Waiting in userspace lets
    // PID 0 do firmware work without keeping this syscall on the stack.
    do {
        if(ioctl(fd,KWIFI_RESULT,(unsigned long)&result)<0){perror("wifi result");close(fd);return 1;}
        if(!result.complete)msleep(100);
    }while(!result.complete);
    close(fd);
    if(result.error){dprintf(2,"wifi: operation failed (%d); run wifi status\n",result.error);return 1;}
    if(request==KWIFI_SCAN){
        printf("BSSID              CH  dBm  security  SSID\n");
        for(unsigned int i=0;i<result.count && i<KWIFI_MAX_RESULTS;i++){
            struct kwifi_network* n=&result.networks[i];
            printf("%02x:%02x:%02x:%02x:%02x:%02x %3u %4d  %-8s  ",n->bssid[0],n->bssid[1],n->bssid[2],n->bssid[3],n->bssid[4],n->bssid[5],n->channel,n->signal_dbm,n->privacy?"secured":"open");
            safe_ssid(n->ssid,n->ssid_length);printf("\n");
        }
        printf("%u network(s)\n",result.count);
    }else printf("%s\n",request==KWIFI_JOIN?"Wi-Fi associated; DHCP will configure the address.":"Wi-Fi disconnected.");
    return 0;
}
