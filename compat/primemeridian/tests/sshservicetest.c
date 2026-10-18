// This file has been generated in whole or in part by a Large Language Model
// Persistent Habilis SSH checks, coordinated by buildutils/ssh_service_test.py.
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/random.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <pwd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned short fixture_port;
static FILE* serial;
static int passed, failed;
#define CHECK(name, expression) do { int ok = !!(expression); passed += ok; failed += !ok; \
    printf("%s %s\n", ok ? "PASS" : "FAIL", name); \
    if (serial) { fprintf(serial, "%s %s\n", ok ? "PASS" : "FAIL", name); fflush(serial); } } while (0)

static long long milliseconds(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return 0;
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static int write_all(int fd, const void* data, size_t length) {
    const unsigned char* bytes = data;
    while (length) {
        ssize_t n = write(fd, bytes, length);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        bytes += n; length -= (size_t)n;
    }
    return 0;
}
static int connect_to(unsigned short port, unsigned address) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port), .sin_addr.s_addr = address };
    if (connect(fd, (void*)&a, sizeof(a)) < 0) {
        if (errno != EINPROGRESS) { close(fd); return -1; }
        struct pollfd p = { .fd = fd, .events = POLLOUT };
        int error = 0; socklen_t size = sizeof(error);
        if (poll(&p, 1, 1000) <= 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) || error) {
            close(fd); return -1;
        }
    }
    if (fcntl(fd, F_SETFL, 0)) { close(fd); return -1; }
    struct timeval timeout = { .tv_sec = 3 };
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout))) { close(fd); return -1; }
    return fd;
}
// Keys are small. Reject incomplete bodies and oversized responses before any
// credential file is created; no HTTP redirect or arbitrary destination exists.
static int fetch(const char* path, const char* destination) {
    long long deadline = milliseconds() + 30000;
    int fd = -1;
    do {
        fd = connect_to(fixture_port, inet_addr("10.0.2.2"));
        if (fd >= 0) break;
        usleep(200000);
    } while (milliseconds() < deadline);
    if (fd < 0) return -1;
    char request[256];
    int size = snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\nHost: 10.0.2.2\r\nConnection: close\r\n\r\n", path);
    if (size < 0 || size >= (int)sizeof(request) || write_all(fd, request, (size_t)size)) { close(fd); return -1; }
    unsigned char response[16385]; size_t used = 0;
    for (;;) {
        if (used == sizeof(response) - 1) { close(fd); return -1; }
        ssize_t n = read(fd, response + used, sizeof(response) - 1 - used);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { close(fd); return -1; }
        if (!n) break;
        used += (size_t)n;
    }
    close(fd); response[used] = 0;
    if (strncmp((char*)response, "HTTP/1.0 200 ", 13) && strncmp((char*)response, "HTTP/1.1 200 ", 13)) return -1;
    char* body = strstr((char*)response, "\r\n\r\n");
    char* field = strstr((char*)response, "\r\nContent-Length: ");
    if (!body || !field || field >= body) return -1;
    char* end;
    unsigned long length = strtoul(field + strlen("\r\nContent-Length: "), &end, 10);
    body += 4;
    if (strncmp(end, "\r\n", 2) || !length || length > 8192 || length != used - (size_t)(body - (char*)response)) return -1;
    int output = open(destination, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (output < 0) return -1;
    int result = write_all(output, body, length);
    if (close(output)) result = -1;
    if (result) unlink(destination);
    return result;
}
static int wait_child(pid_t child, int seconds) {
    long long deadline = milliseconds() + seconds * 1000LL;
    int status;
    while (milliseconds() < deadline) {
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        if (result < 0 && errno != EINTR) return -1;
        usleep(100000);
    }
    kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    return -1;
}
static pid_t launch(char* const arguments[], const char* output) {
    pid_t child = fork();
    if (child) return child;
    if (serial) close(fileno(serial));
    int log = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (log < 0 || dup2(log, STDOUT_FILENO) < 0 || dup2(log, STDERR_FILENO) < 0) _exit(126);
    if (log > STDERR_FILENO) close(log);
    int input = open("/dev/null", O_RDONLY);
    if (input < 0 || dup2(input, STDIN_FILENO) < 0) _exit(126);
    if (input > STDERR_FILENO) close(input);
    execv(arguments[0], arguments);
    _exit(127);
}
static int command(char *const args[]) {
    pid_t child=launch(args,"/tmp/ssh-service-command.log");
    int result=child>0?wait_child(child,40):-1;
    if(result){
        FILE *log=fopen("/tmp/ssh-service-command.log","r");char line[256];
        if(log){while(fgets(line,sizeof(line),log)){fputs(line,stdout);if(serial){fputs(line,serial);fflush(serial);}}fclose(log);}
    }
    return result;
}
static int listening(void) {
    int fd=connect_to(22,htonl(INADDR_LOOPBACK));
    if(fd<0)return 0;
    close(fd);return 1;
}
static int wait_listener(int wanted) {
    long long deadline=milliseconds()+20000;
    do {if(listening()==wanted)return 1;usleep(200000);} while(milliseconds()<deadline);
    return 0;
}
static int protected_file(const char *path) {
    struct stat st;return !lstat(path,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&(st.st_mode&0777)==0600;
}
int main(int argc,char **argv) {
    serial=fopen("/dev/serial","w");
    int phase=argc==3?atoi(argv[1]):0;
    fixture_port=argc==3?(unsigned short)atoi(argv[2]):0;
    CHECK("SSH service phase",getuid()==0&&(phase==1||phase==2)&&fixture_port>1024);
    if(failed)goto done;
    struct stat root,home;
    CHECK("SSH separate persistent home",!stat("/",&root)&&!stat("/home",&home)&&root.st_dev!=home.st_dev);
    if(failed)goto done;
    if(phase==1) {
        CHECK("SSH starts disabled",!listening()&&access("/home/.kunix/ssh-enabled",F_OK)!=0);
        char *enable[]={"/bin/sshctl","enable",NULL};
        CHECK("SSH refuses shipped empty password",command(enable)!=0);
        if(failed)goto done;
        CHECK("SSH disposable public key",(mkdir("/home/root/.ssh",0700)==0||errno==EEXIST)&&fetch("/authorized_keys","/home/root/.ssh/authorized_keys")==0);
        CHECK("SSH explicit enable",command(enable)==0);
        if(failed)goto done;
    } else {
        CHECK("SSH enable persists across reboot",protected_file("/home/.kunix/ssh-enabled"));
        CHECK("SSH public key persists across reboot",protected_file("/home/root/.ssh/authorized_keys"));
    }
    CHECK("SSH manager starts listener",wait_listener(1));
    CHECK("SSH protected persistent host identity",protected_file("/home/.kunix/ssh_host_ed25519_key"));
    if(failed)goto done;
    fprintf(serial,"sshservicetest: server ready phase %d\n",phase);fflush(serial);
    long long deadline=milliseconds()+60000;
    while(access("/tmp/ssh-service-inbound-ok",F_OK)&&milliseconds()<deadline)usleep(200000);
    CHECK("SSH host OpenSSH PTY command",access("/tmp/ssh-service-inbound-ok",F_OK)==0);
    char *disable[]={"/bin/sshctl","disable",NULL};
    CHECK("SSH disable stops listener",command(disable)==0&&wait_listener(0));
    CHECK("SSH disable removes enable state",access("/home/.kunix/ssh-enabled",F_OK)!=0);
    if(phase==1){
        char *enable[]={"/bin/sshctl","enable",NULL};
        CHECK("SSH reenable before reboot",command(enable)==0&&wait_listener(1));
    }
    sync();
done:
    if(failed&&serial){
        FILE *log=fopen("/var/log/habilis.log","r");char line[256];unsigned shown=0;
        if(log){while(fgets(line,sizeof(line),log)&&shown<8192){fputs(line,serial);shown+=(unsigned)strlen(line);}fclose(log);}
    }
    printf("sshservicetest: %d passed, %d failed\n",passed,failed);
    if(serial){fprintf(serial,"sshservicetest: %d passed, %d failed\n",passed,failed);fclose(serial);}
    return failed?1:0;
}
