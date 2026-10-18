/* This file has been generated in whole or in part by a Large Language Model */
#define NETCONFIG_OWNER_UID getuid()
#define geteuid test_geteuid
#define stat(...) test_stat(__VA_ARGS__)
#define lstat(...) test_lstat(__VA_ARGS__)
#define unlink test_unlink
#define execv test_execv
#define open test_open
#define ioctl test_ioctl
#define usleep test_usleep
#define main netconfig_main
#include "../../network/netconfig.c"
#undef main
#undef geteuid
#undef stat
#undef lstat
#undef unlink
#undef execv
#undef open
#undef ioctl
#undef usleep
#include <stdarg.h>
extern int open(const char *,int,...);
extern int usleep(useconds_t);
#include <assert.h>
extern int stat(const char *,struct stat *);
extern int lstat(const char *,struct stat *);
extern int unlink(const char *);
static int fail_remove, wifi_submissions;
static struct kwifi_join replacement, last_submission;
int test_open(const char *path,int flags,...){
    mode_t mode=0;if(flags&O_CREAT){va_list args;va_start(args,flags);mode=(mode_t)va_arg(args,int);va_end(args);}
    return open(!strcmp(path,"/dev/wifi0")?"/dev/null":path,flags,mode);
}
int test_usleep(useconds_t duration){(void)duration;return 0;}
int test_ioctl(int fd,unsigned long request,...){
    (void)fd;va_list args;va_start(args,request);void *pointer=va_arg(args,void*);va_end(args);
    if(request==KWIFI_RESULT){struct kwifi_result *r=pointer;r->complete=1;return 0;}
    assert(request==KWIFI_JOIN);last_submission=*(struct kwifi_join*)pointer;
    /* A completed manual join published a new profile before this old
       automatic request reached the driver. */
    if(!wifi_submissions++)assert(!save_profile(&replacement));
    return wifi_submissions;
}
uid_t test_geteuid(void){return 0;}
int test_stat(const char *path,struct stat *st){
    int rc=stat(path,st);
    if(!strcmp(path,"/")){memset(st,0,sizeof(*st));st->st_mode=S_IFDIR|0755;st->st_dev=1;return 0;}
    return rc;
}
int test_lstat(const char *path,struct stat *st){
    if(!strcmp(path,"/home")){memset(st,0,sizeof(*st));st->st_mode=S_IFDIR|0755;st->st_dev=2;return 0;}
    return lstat(path,st);
}
int test_unlink(const char *path){
    if(fail_remove&&strstr(path,"/ssh-enabled")){errno=EACCES;return -1;}
    return unlink(path);
}
int test_execv(const char *file,char *const args[]){
    if(strcmp(file,"/bin/dropbearkey")){errno=ENOENT;return -1;}
    int fd=open(args[4],O_WRONLY|O_CREAT|O_EXCL,0600);assert(fd>=0);
    assert(write(fd,"loser",5)==5);assert(!close(fd));
    /* A concurrent enable wins publication while this key is generated. */
    assert(!atomic_save("ssh_host_ed25519_key","winner",6));
    _exit(0);
}
int main(int argc,char **argv){
    char dir[]="/tmp/kunix-network-regression-XXXXXX";assert(mkdtemp(dir));
    snprintf(state_dir,sizeof(state_dir),"%s",dir);
    assert(argc==2);
    if(!strcmp(argv[1],"key-race")){
        assert(!host_key());char key[7]={0};int fd=protected_open("ssh_host_ed25519_key");assert(fd>=0);
        assert(read(fd,key,6)==6);close(fd);assert(!strcmp(key,"winner"));
        remove_state("ssh_host_ed25519_key");
    }else if(!strcmp(argv[1],"wifi-race")){
        struct kwifi_join old={.ssid_length=3};memcpy(old.ssid,"old",3);
        replacement.ssid_length=3;memcpy(replacement.ssid,"new",3);
        assert(!save_profile(&old));automatic_wifi=1;struct kwifi_result result;
        assert(!wifi_job(KWIFI_JOIN,&old,&result));
        assert(wifi_submissions==2&&!memcmp(last_submission.ssid,"new",3));
        remove_state("wifi");
    }else{
        assert(!atomic_save("ssh-enabled","",0));fail_remove=1;char *disable[]={"disable"};
        assert(ssh_command(1,disable)!=0);assert(ssh_enabled());
        fail_remove=0;remove_state("ssh-enabled");
    }
    assert(!rmdir(dir));puts("network regression passed");
}
