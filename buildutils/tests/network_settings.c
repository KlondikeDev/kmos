/* This file has been generated in whole or in part by a Large Language Model */
#define NETCONFIG_OWNER_UID geteuid()
#define open test_open
#define ioctl test_ioctl
#define usleep test_usleep
#define main netconfig_main
#include "../../network/netconfig.c"
#undef main
#undef open
#undef ioctl
#undef usleep
#include <assert.h>
#include <stdarg.h>
extern int open(const char *,int,...);
extern int usleep(useconds_t);
static int fake_device, busy_replies, submissions;
int test_open(const char *path,int flags,...) {
    mode_t mode=0;
    if(flags&O_CREAT){va_list args;va_start(args,flags);mode=(mode_t)va_arg(args,int);va_end(args);}
    return open(fake_device&&!strcmp(path,"/dev/wifi0")?"/dev/null":path,flags,mode);
}
int test_ioctl(int fd,unsigned long request,...) {
    (void)fd;va_list args;va_start(args,request);void *pointer=va_arg(args,void*);va_end(args);
    if(request==KWIFI_RESULT){struct kwifi_result *r=pointer;r->complete=1;r->error=0;return 0;}
    submissions++;
    if(busy_replies-->0){errno=EBUSY;return -1;}
    return 7;
}
int test_usleep(useconds_t duration){return fake_device?0:usleep(duration);}

int main(void) {
    char dir[]="/tmp/kunix-network-XXXXXX";
    assert(mkdtemp(dir));
    snprintf(state_dir,sizeof(state_dir),"%s",dir);
    struct kwifi_join original={.ssid_length=7,.passphrase_length=8};
    memcpy(original.ssid,"test\nAP",7); memcpy(original.passphrase,"secret!x",8);
    assert(save_profile(&original)==0);
    struct stat st; char path[512]; state_path(path,sizeof(path),"wifi");
    assert(!stat(path,&st) && (st.st_mode&0777)==0600);
    struct kwifi_join loaded={0}; assert(load_profile(&loaded)==0);
    assert(!memcmp(&original,&loaded,sizeof(original)));
    /* A fresh process reloads exactly the same protected profile. */
    pid_t child=fork(); assert(child>=0);
    if(!child){struct kwifi_join reboot={0}; _exit(load_profile(&reboot)||memcmp(&original,&reboot,sizeof(original)));}
    int result; assert(waitpid(child,&result,0)==child && WIFEXITED(result) && !WEXITSTATUS(result));
    assert(!chmod(path,0644)); assert(load_profile(&loaded)<0); assert(!chmod(path,0600));
    assert(!unlink(path)); assert(!symlink("/etc/passwd",path)); assert(load_profile(&loaded)<0); assert(!unlink(path));
    original.ssid_length=33; assert(save_profile(&original)<0);
    /* Manual joins wait until the active daemon job releases the driver. */
    struct kwifi_result wifi_result;
    fake_device=1;busy_replies=3;submissions=0;automatic_wifi=0;
    assert(wifi_job(KWIFI_JOIN,&loaded,&wifi_result)==0&&submissions==4);
    assert(atomic_save("wifi-paused","",0)==0);
    automatic_wifi=1;submissions=0;
    assert(wifi_job(KWIFI_JOIN,&loaded,&wifi_result)<0&&errno==ECANCELED&&submissions==0);
    remove_state("wifi-paused");automatic_wifi=0;fake_device=0;
    assert(!password_hash_ok("")); assert(!password_hash_ok("!")); assert(!password_hash_ok("*"));
    assert(password_hash_ok("$6$salt$hash"));
    assert(retry_delay(0)==1 && retry_delay(5)==32 && retry_delay(20)==60);
    assert(atomic_save("ssh-enabled","",0)==0); assert(ssh_enabled());
    state_path(path,sizeof(path),"ssh-enabled"); assert(!chmod(path,0644)); assert(!ssh_enabled()); assert(!unlink(path));
    /* A saved host identity is reused; generation must never replace it. */
    assert(atomic_save("ssh_host_ed25519_key","identity",8)==0); assert(host_key()==0);
    int fd=protected_open("ssh_host_ed25519_key"); char identity[9]={0}; assert(fd>=0&&read(fd,identity,8)==8); close(fd); assert(!strcmp(identity,"identity"));
    state_path(path,sizeof(path),"ssh_host_ed25519_key"); assert(!unlink(path));
    /* Both boot managers remain idle without usable state and stop cleanly. */
    for(int which=0;which<2;which++){
        child=fork(); assert(child>=0);
        if(!child){signal(SIGTERM,stop);_exit(which?ssh_daemon():wifi_daemon());}
        usleep(50000); assert(!kill(child,SIGTERM));
        assert(waitpid(child,&result,0)==child && WIFEXITED(result) && !WEXITSTATUS(result));
    }
    assert(!rmdir(dir)); puts("network settings: persistence, permissions, symlink rejection, authentication and backoff passed");
}
