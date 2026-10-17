// This file has been generated in whole or in part by a Large Language Model
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
static int passed, failed;
static FILE* serial;
#define CHECK(name, expr) do { int ok = (expr); passed += ok; failed += !ok; \
 printf("%s %s\n", ok ? "PASS" : "FAIL", name); \
 if (serial) { fprintf(serial,"%s %s\n",ok ? "PASS" : "FAIL",name); fflush(serial); } } while(0)
static long resident(void) {
 FILE* f=fopen("/proc/self/status","r"); char line[160]; long kb=-1;
 if (!f) return kb;
 while(fgets(line,sizeof(line),f)) if(sscanf(line,"VmRSS: %ld kB",&kb)==1) break;
 fclose(f); return kb;
}
static int child_status(pid_t p) { int st=0; if(p<0 || waitpid(p,&st,0)!=p) return -1; return st; }
int main(void) {
 serial=fopen("/dev/serial","w");
 const size_t length=256UL*1024*1024;
 int fd=open("/vmtest.data",O_RDWR|O_CREAT|O_TRUNC,0600);
 CHECK("make a sparse file",fd>=0 && ftruncate(fd,length)==0 && pwrite(fd,"hello",5,0)==5);
 long before=resident();
 char* p=mmap(NULL,length,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0);
 long after=resident();
 CHECK("large file mmap is lazy",p!=MAP_FAILED && before>=0 && after-before<64);
 if(p!=MAP_FAILED) {
  CHECK("file bytes and sparse zero page fault in",!memcmp(p,"hello",5) && p[length-1]==0);
  close(fd); fd=-1;
  CHECK("mapping survives close and unlink",unlink("/vmtest.data")==0 && p[1]=='e');
  pid_t child=fork();
  if(child==0) { p[0]='C'; p[length-1]=7; _exit(p[0]=='C' ? 0:1); }
  int st=child_status(child);
  CHECK("file mapping fork has private writes",WIFEXITED(st) && WEXITSTATUS(st)==0 && p[0]=='h' && p[length-1]==0);
  CHECK("split mapping mprotect",mprotect(p+4096,4096,PROT_NONE)==0 && mprotect(p+4096,4096,PROT_READ)==0 && p[4096]==0);
  CHECK("partial unmap preserves backing",munmap(p+4096,4096)==0 && p[8192]==0);
  munmap(p,length);
 }
 if(fd>=0) close(fd);
 unlink("/vmtest.data");
 fd=open("/vmtest.small",O_RDWR|O_CREAT|O_TRUNC,0600);
 write(fd,"abc",3);
 char* target=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0);
 int channel[2]; pipe(channel); write(channel[1],"Z",1);
 CHECK("syscall first-touch write copies a clean file page",target!=MAP_FAILED && read(channel[0],target,1)==1 && target[0]=='Z');
 close(channel[0]); close(channel[1]);
 if(target!=MAP_FAILED) munmap(target,4096);
 char original=0;
 CHECK("first-touch write stays private",pread(fd,&original,1,0)==1 && original=='a');
 target=mmap(NULL,4096,PROT_NONE,MAP_PRIVATE,fd,0);
 CHECK("untouched file mapping can gain read permission",target!=MAP_FAILED && mprotect(target,4096,PROT_READ)==0 && target[1]=='b');
 if(target!=MAP_FAILED) munmap(target,4096);
 target=mmap(NULL,4096,PROT_READ,MAP_PRIVATE,fd,0);
 CHECK("cached page starts with file data",target!=MAP_FAILED && target[0]=='a');
 pwrite(fd,"new",3,0);
 char* newer=mmap(NULL,4096,PROT_READ,MAP_PRIVATE,fd,0);
 CHECK("new mappings see file writes",newer!=MAP_FAILED && newer[0]=='n');
 if(target!=MAP_FAILED) munmap(target,4096);
 if(newer!=MAP_FAILED) munmap(newer,4096);
 pwrite(fd,"abc",3,0);
 p=mmap(NULL,8192,PROT_READ,MAP_PRIVATE,fd,0);
 CHECK("tail of a file page is zero",p!=MAP_FAILED && p[2]=='c' && p[3]==0 && p[4095]==0);
 pid_t child=fork();
 if(child==0) { volatile char c=p!=MAP_FAILED ? p[4096] : 0; (void)c; _exit(1); }
 int st=child_status(child);
 CHECK("page wholly beyond EOF gives SIGBUS",WIFSIGNALED(st) && WTERMSIG(st)==SIGBUS);
 if(p!=MAP_FAILED) munmap(p,8192);
 close(fd); unlink("/vmtest.small");
 child=fork();
 if(child==0) { execl("/bin/pagingbig","pagingbig",NULL); _exit(1); }
 st=child_status(child);
 CHECK("large ELF BSS is demand paged",WIFEXITED(st) && WEXITSTATUS(st)==42);
 errno=0; fd=open("/bin/vmtest",O_WRONLY);
 CHECK("running executable cannot be opened for writing",fd<0 && errno==ETXTBSY);
 if(fd>=0) close(fd);
 // A failed exec must not discard the caller's mappings.
 p=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 if(p!=MAP_FAILED) p[0]=19;
 execl("/test/hello.txt","bad",NULL);
 CHECK("failed exec retains VMAs",p!=MAP_FAILED && p[0]==19 && mprotect(p,4096,PROT_READ)==0);
 if(p!=MAP_FAILED) munmap(p,4096);
 printf("vmtest: %d passed, %d failed\n",passed,failed);
 if(serial) { fprintf(serial,"vmtest: %d passed, %d failed\n",passed,failed); fclose(serial); }
 return failed!=0;
}
