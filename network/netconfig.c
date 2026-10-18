/* This file has been generated in whole or in part by a Large Language Model */
/* Persistent network settings; built with musl for the Linux personality. */
#define _GNU_SOURCE
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shadow.h>
#include "../include/wifi_ioctl.h"

#ifndef NETCONFIG_OWNER_UID
#define NETCONFIG_OWNER_UID 0
#endif

static char state_dir[256]="/home/.kunix";
static volatile sig_atomic_t stopping;
static pid_t server_pid;
static void erase(void *p,size_t n){volatile unsigned char *v=p;while(n--)*v++=0;}
static void state_path(char *out,size_t n,const char *name){snprintf(out,n,"%s/%s",state_dir,name);}
static int protected_stat(const struct stat *s,int directory){
    return s->st_uid==NETCONFIG_OWNER_UID && (s->st_mode&0777)==(directory?0700:0600) &&
        (directory?S_ISDIR(s->st_mode):S_ISREG(s->st_mode)) && (directory || s->st_nlink==1);
}
static int state_ready(void){
    struct stat root,home,dir;
    if(geteuid()!=0){errno=EPERM;return -1;}
    /* The root image also contains /home/root: existence alone proves nothing. */
    if(stat("/",&root)||lstat("/home",&home)||!S_ISDIR(home.st_mode)||home.st_dev==root.st_dev||home.st_uid!=0){errno=ENODEV;return -1;}
    /* Older home images were seeded with the host umask (often 0775).
       Secure the root-owned mount directory before trusting its children. */
    if((home.st_mode&0022)&&chmod("/home",home.st_mode&07755))return -1;
    if(mkdir(state_dir,0700)&&errno!=EEXIST)return -1;
    if(lstat(state_dir,&dir)||!protected_stat(&dir,1)){errno=EACCES;return -1;}
    /* Probe once per process/device: an existing directory on a read-only
       home must not let an already-enabled listener start. */
    static dev_t writable_device; static int checked;
    if(!checked || writable_device!=home.st_dev){
        char probe[512];snprintf(probe,sizeof(probe),"%s/.write-test.%ld",state_dir,(long)getpid());
        int fd=open(probe,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
        if(fd<0)return -1;
        int rc=write(fd,"x",1)==1?0:-1;close(fd);
        if(unlink(probe))rc=-1;
        if(rc)return -1;
        writable_device=home.st_dev;checked=1;
    }
    return 0;
}
static int protected_open(const char *name){
    char path[512];struct stat st;state_path(path,sizeof(path),name);
    if(lstat(path,&st))return -1;
    if(!protected_stat(&st,0)){errno=EACCES;return -1;}
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return -1;
    if(fstat(fd,&st)||!protected_stat(&st,0)){close(fd);errno=EACCES;return -1;}
    return fd;
}
static int atomic_save(const char *name,const void *data,size_t length){
    char path[512],tmp[550]; state_path(path,sizeof(path),name);
    snprintf(tmp,sizeof(tmp),"%s.tmp.%ld",path,(long)getpid());
    int fd=open(tmp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if(fd<0)return -1;
    size_t done=0;int rc=-1;
    while(done<length){ssize_t n=write(fd,(const char*)data+done,length-done);if(n<=0)goto out;done+=(size_t)n;}
    if(fsync(fd)||rename(tmp,path))goto out;
    rc=0;
    /* sync the directory as well; unsupported directory fsync is harmless. */
    int dir=open(state_dir,O_RDONLY|O_DIRECTORY);if(dir>=0){fsync(dir);close(dir);}
out: close(fd);if(rc)unlink(tmp);return rc;
}
static int valid_profile(const struct kwifi_join *p){
    return p->ssid_length>0 && p->ssid_length<=32 &&
        (!p->passphrase_length || (p->passphrase_length>=8 && p->passphrase_length<=63));
}
static int save_profile(const struct kwifi_join *p){
    if(!valid_profile(p)){errno=EINVAL;return -1;}
    return atomic_save("wifi",p,sizeof(*p));
}
static int load_profile(struct kwifi_join *p){
    int fd=protected_open("wifi");if(fd<0)return -1;
    size_t done=0;while(done<sizeof(*p)){ssize_t n=read(fd,(char*)p+done,sizeof(*p)-done);if(n<=0)break;done+=(size_t)n;}
    char tail;int extra=read(fd,&tail,1);close(fd);
    if(done!=sizeof(*p)||extra!=0||!valid_profile(p)){erase(p,sizeof(*p));errno=EINVAL;return -1;}
    p->passphrase[p->passphrase_length]=0;return 0;
}
static int exists(const char *name){char path[512];struct stat st;state_path(path,sizeof(path),name);return !lstat(path,&st);}
static int remove_state(const char *name){
    char path[512];state_path(path,sizeof(path),name);
    if(unlink(path)&&errno!=ENOENT)return -1;
    sync();return 0;
}
static unsigned retry_delay(unsigned failures){return failures>=6?60:1u<<failures;}
static void stop(int sig){(void)sig;stopping=1;if(server_pid>0)kill(server_pid,SIGTERM);}
static void pause_seconds(unsigned seconds){while(seconds--&&!stopping)sleep(1);}
static int automatic_wifi;
static int wifi_job(unsigned request,struct kwifi_join *profile,struct kwifi_result *result){
    int fd=open("/dev/wifi0",O_RDWR);if(fd<0)return -1;
submit:
    ;
    int job=-1;
    /* A manual command first pauses recovery, then waits for an already
       running automatic job to release the driver's single operation slot. */
    for(unsigned busy=0;busy<1200 && !stopping;busy++){
        if(automatic_wifi && exists("wifi-paused")){errno=ECANCELED;break;}
        if(automatic_wifi && request==KWIFI_JOIN){
            struct kwifi_join latest={0};
            if(load_profile(&latest)){erase(&latest,sizeof(latest));break;}
            *profile=latest;erase(&latest,sizeof(latest));
            if(exists("wifi-paused")){errno=ECANCELED;break;}
        }
        job=ioctl(fd,request,request==KWIFI_JOIN?profile:NULL);
        if(job>=0||errno!=EBUSY)break;
        usleep(100000);
    }
    if(job<0){close(fd);return -1;}memset(result,0,sizeof(*result));result->job=(unsigned)job;
    unsigned attempts=0;
    do {
        if(ioctl(fd,KWIFI_RESULT,result)<0){close(fd);return -1;}
        if(!result->complete)usleep(100000);
    }while(!result->complete && !stopping && ++attempts<1200);
    if(!result->complete||result->error){close(fd);errno=result->error?-result->error:ETIMEDOUT;return -1;}
    if(automatic_wifi && request==KWIFI_JOIN && !exists("wifi-paused")){
        /* A manual join can finish after our pause check but before our ioctl.
           Reconcile after completion so its saved profile wins that race. */
        struct kwifi_join latest={0};
        if(load_profile(&latest)){erase(&latest,sizeof(latest));close(fd);return -1;}
        int changed=memcmp(profile,&latest,sizeof(latest));
        if(changed)*profile=latest;
        erase(&latest,sizeof(latest));
        if(changed)goto submit;
    }
    close(fd);return 0;
}
static int wifi_link(void){
    FILE *f=fopen("/proc/wifi","r");if(!f)return 0;
    char line[160];int up=0;while(fgets(line,sizeof(line),f))if(!strcmp(line,"link: 3\n"))up=1;
    fclose(f);return up;
}
static struct termios saved_terminal;
static int terminal_changed;
static void restore_terminal(void){if(terminal_changed){tcsetattr(0,TCSANOW,&saved_terminal);terminal_changed=0;}}
static void password_stop(int sig){restore_terminal();_exit(128+sig);}
static int read_password(struct kwifi_join *p){
    if(tcgetattr(0,&saved_terminal)){fputs("wifi: passphrase requires a terminal\n",stderr);return -1;}
    struct termios mode=saved_terminal;mode.c_lflag&=~ECHO;
    if(tcsetattr(0,TCSANOW,&mode))return -1;
    terminal_changed=1;signal(SIGINT,password_stop);signal(SIGTERM,password_stop);signal(SIGHUP,password_stop);signal(SIGQUIT,password_stop);signal(SIGTSTP,SIG_IGN);
    fputs("Passphrase: ",stdout);fflush(stdout);
    char c;unsigned n=0;int overflow=0;ssize_t got;
    while((got=read(0,&c,1))==1 && c!='\n'){if(n<63)p->passphrase[n++]=c;else overflow=1;}
    restore_terminal();putchar('\n');
    if(got!=1||overflow||n<8){erase(p->passphrase,sizeof(p->passphrase));fputs("wifi: passphrase must contain 8 to 63 bytes\n",stderr);return -1;}
    p->passphrase_length=n;return 0;
}
static int wifi_command(int argc,char **argv){
    struct kwifi_join p={0};struct kwifi_result result;
    if(argc==0 || (argc==1&&!strcmp(argv[0],"status"))){
        FILE *f=fopen("/proc/wifi","r");if(!f){perror("wifi status");return 1;}char line[256];while(fgets(line,sizeof(line),f))fputs(line,stdout);fclose(f);return 0;
    }
    if(argc==1&&!strcmp(argv[0],"scan")){
        if(wifi_job(KWIFI_SCAN,NULL,&result)){perror("wifi scan");return 1;}
        puts("BSSID              CH  dBm  security  SSID");
        for(unsigned i=0;i<result.count&&i<KWIFI_MAX_RESULTS;i++){
            struct kwifi_network *n=&result.networks[i];
            printf("%02x:%02x:%02x:%02x:%02x:%02x %3u %4d  %-8s  ",n->bssid[0],n->bssid[1],n->bssid[2],n->bssid[3],n->bssid[4],n->bssid[5],n->channel,n->signal_dbm,n->privacy?"secured":"open");
            for(unsigned j=0;j<n->ssid_length&&j<32;j++){unsigned c=n->ssid[j];if(c>=32&&c<=126)putchar((int)c);else printf("\\x%02x",c);}putchar('\n');
        }return 0;
    }
    if(state_ready()){perror("wifi: secure writable home disk required");return 1;}
    if(argc==1&&(!strcmp(argv[0],"disconnect")||!strcmp(argv[0],"forget"))){
        if(atomic_save("wifi-paused","",0)){perror("wifi pause");return 1;}
        if(!strcmp(argv[0],"forget")&&remove_state("wifi")){perror("wifi forget");return 1;}
        if(wifi_job(KWIFI_DISCONNECT,NULL,&result)){perror("wifi disconnect");return 1;}
        puts("Wi-Fi disconnected; automatic reconnection paused. Use wifi reconnect or wifi join.");return 0;
    }
    if(argc==1&&!strcmp(argv[0],"reconnect")){
        if(load_profile(&p)){perror("wifi profile");return 1;}
    }else if((argc==2 || (argc==3&&!strcmp(argv[2],"--open")))&&!strcmp(argv[0],"join")){
        size_t n=strlen(argv[1]);if(!n||n>32){fputs("wifi: SSID must contain 1 to 32 bytes\n",stderr);return 1;}
        p.ssid_length=(unsigned)n;memcpy(p.ssid,argv[1],n);
        if(argc==2&&read_password(&p))return 1;
    }else{fputs("usage: wifi [status|scan|join SSID [--open]|disconnect|reconnect|forget]\n",stderr);return 1;}
    if(atomic_save("wifi-paused","",0)){erase(&p,sizeof(p));perror("wifi pause");return 1;}
    int rc=wifi_job(KWIFI_JOIN,&p,&result);
    if(!rc)rc=save_profile(&p);
    erase(&p,sizeof(p));
    if(rc){perror("wifi join/save (automatic reconnection remains paused)");return 1;}
    if(remove_state("wifi-paused")){perror("wifi: profile saved but automatic reconnection remains paused");return 1;}
    puts("Wi-Fi associated; profile saved. DHCP will configure the address.");return 0;
}
static int wifi_daemon(void){
    automatic_wifi=1;
    unsigned failures=0;
    while(!stopping){
        struct kwifi_join p={0};struct kwifi_result result;
        if(state_ready()||exists("wifi-paused")||load_profile(&p)){erase(&p,sizeof(p));pause_seconds(5);continue;}
        if(wifi_link()){failures=0;erase(&p,sizeof(p));pause_seconds(3);continue;}
        int rc=wifi_job(KWIFI_JOIN,&p,&result);erase(&p,sizeof(p));
        if(rc){pause_seconds(retry_delay(failures));if(failures<6)failures++;}else{failures=0;pause_seconds(3);}
    }return 0;
}
static int password_hash_ok(const char *hash){return hash&&*hash&&*hash!='!'&&*hash!='*';}
static int ssh_auth_ready(void){
    struct spwd *root=getspnam("root");
    if(root&&password_hash_ok(root->sp_pwdp))return 1;
    /* A public key can enable SSH even while root's console password is empty. */
    struct stat st;const char *dirs[]={"/home/root","/home/root/.ssh"};
    for(unsigned i=0;i<2;i++)if(lstat(dirs[i],&st)||!S_ISDIR(st.st_mode)||st.st_uid!=0||(st.st_mode&0022))return 0;
    int fd=open("/home/root/.ssh/authorized_keys",O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return 0;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=0||(st.st_mode&0022)){close(fd);return 0;}
    FILE *f=fdopen(fd,"r");if(!f){close(fd);return 0;}
    char line[8192];int found=0;
    while(fgets(line,sizeof(line),f))if(!strncmp(line,"ssh-ed25519 ",12)||!strncmp(line,"ssh-rsa ",8)||!strncmp(line,"ecdsa-sha2-",11)){found=1;break;}
    fclose(f);return found;
}
static int run(char *const argv[]){
    pid_t child=fork();if(child<0)return -1;
    if(!child){execv(argv[0],argv);_exit(127);}
    int status;while(waitpid(child,&status,0)<0){if(errno!=EINTR)return -1;}
    return WIFEXITED(status)&&WEXITSTATUS(status)==0?0:-1;
}
static int host_key(void){
    int fd=protected_open("ssh_host_ed25519_key");if(fd>=0){close(fd);return 0;}
    if(errno!=ENOENT)return -1;
    char key[512],tmp[550];state_path(key,sizeof(key),"ssh_host_ed25519_key");snprintf(tmp,sizeof(tmp),"%s.tmp.%ld",key,(long)getpid());
    char *args[]={"/bin/dropbearkey","-t","ed25519","-f",tmp,NULL};
    /* Dropbearkey emits the public key only; private material stays in the file. */
    if(run(args)){unlink(tmp);return -1;}
    fd=open(tmp,O_RDONLY|O_NOFOLLOW);struct stat st;
    if(fd<0)return -1;
    int bad=fstat(fd,&st)||!protected_stat(&st,0)||fsync(fd);close(fd);
    if(bad){unlink(tmp);return -1;}
    /* Publish without replacing an identity created by another enable/manager
       while key generation was in progress. rename() would overwrite it. */
    int published=link(tmp,key),publish_error=errno;
    if(unlink(tmp))return -1;
    if(published&&publish_error!=EEXIST){errno=publish_error;return -1;}
    fd=protected_open("ssh_host_ed25519_key");if(fd<0)return -1;
    close(fd);sync();return 0;
}
static int ssh_enabled(void);
static int ssh_command(int argc,char **argv){
    if(state_ready()){perror("sshctl: secure writable home disk required");return 1;}
    if(argc==1&&!strcmp(argv[0],"status")){puts(ssh_enabled()?"SSH enabled (Habilis supervises the listener).":"SSH disabled.");return 0;}
    if(argc==1&&!strcmp(argv[0],"disable")){if(remove_state("ssh-enabled")){perror("sshctl disable");return 1;}puts("SSH disabled; listener stops within two seconds.");return 0;}
    if(argc==1&&!strcmp(argv[0],"enable")){
        if(access("/bin/dropbear",X_OK)||access("/bin/dropbearkey",X_OK)){fputs("sshctl: install Dropbear tools first\n",stderr);return 1;}
        if(!ssh_auth_ready()){fputs("sshctl: set a root password with passwd, or install /home/root/.ssh/authorized_keys first\n",stderr);return 1;}
        if(host_key()||atomic_save("ssh-enabled","",0)){perror("sshctl enable");return 1;}
        puts("SSH enabled on TCP port 22; host identity is saved on the home disk.");return 0;
    }
    fputs("usage: sshctl [enable|disable|status]\n",stderr);return 1;
}
static int ssh_enabled(void){int fd=protected_open("ssh-enabled");if(fd<0)return 0;close(fd);return 1;}
static int ssh_daemon(void){
    unsigned failures=0;
    while(!stopping){
        if(state_ready()||!ssh_enabled()||!ssh_auth_ready()){pause_seconds(2);continue;}
        if(host_key()){pause_seconds(30);continue;}
        char key[512];state_path(key,sizeof(key),"ssh_host_ed25519_key");
        server_pid=fork();if(server_pid<0){pause_seconds(5);continue;}
        if(!server_pid){execl("/bin/dropbear","dropbear","-F","-r",key,"-p","22","-P","/var/run/dropbear.pid",(char*)NULL);_exit(127);}
        int status;pid_t got;
        do{got=waitpid(server_pid,&status,WNOHANG);if(got<0&&errno==EINTR){got=0;continue;}if(got==0){if(stopping||state_ready()||!ssh_enabled()||!ssh_auth_ready())kill(server_pid,SIGTERM);pause_seconds(2);}}while(got==0);
        server_pid=0;if(stopping)break;pause_seconds(retry_delay(failures));if(failures<6)failures++;
    }return 0;
}
int main(int argc,char **argv){
    umask(077);signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);
    if(argc<2){fputs("usage: netconfig wifi|ssh|wifi-daemon|ssh-daemon ...\n",stderr);return 1;}
    if(!strcmp(argv[1],"wifi"))return wifi_command(argc-2,argv+2);
    if(!strcmp(argv[1],"ssh"))return ssh_command(argc-2,argv+2);
    if(argc==2&&!strcmp(argv[1],"wifi-daemon"))return wifi_daemon();
    if(argc==2&&!strcmp(argv[1],"ssh-daemon"))return ssh_daemon();
    return 1;
}
