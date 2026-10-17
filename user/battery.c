// This file has been generated in whole or in part by a Large Language Model
// Native Kunix utility; the common proc file also works through ABI layers.
#include "kunix.h"
int main(void) {
    int fd=open("/proc/battery", O_RDONLY);
    if(fd<0) {perror("battery");return 1;}
    char buf[2048]; ssize_t n;
    while((n=read(fd,buf,sizeof(buf)))>0) {
        if(write(STDOUT_FILENO,buf,n)<0) {close(fd);return 1;}
    }
    close(fd);
    if(n<0) {perror("battery");return 1;}
    return 0;
}
