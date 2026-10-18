// This file has been generated in whole or in part by a Large Language Model
#include "kunix.h"
/* The manager uses musl's ownership, termios and secure filesystem interfaces. */
int main(int argc,char **argv,char **envp) {
    char *args[8];
    if(argc>6){dprintf(2,"wifi: too many arguments\n");return 1;}
    args[0]="netconfig";args[1]="wifi";
    for(int i=1;i<argc;i++)args[i+1]=argv[i];
    args[argc+1]=NULL;
    execve("/bin/netconfig",args,envp);
    perror("wifi: /bin/netconfig");return 1;
}
