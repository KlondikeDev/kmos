// This file has been generated in whole or in part by a Large Language Model
// Guest WiFi checks: firmware blob presence and readability, interface
// listing (wlan0 appears only with a live AX200; QEMU reports lo only),
// then (once associated) traffic. Reports over /dev/serial like the
// other suites.
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include "../../../include/wifi_ioctl.h"
_Static_assert(sizeof(struct kwifi_join)==104,"Kunix WiFi join ABI");
_Static_assert(sizeof(struct kwifi_network)==44,"Kunix WiFi scan result ABI");
_Static_assert(sizeof(struct kwifi_result)==1424,"Kunix WiFi job ABI");

static FILE* serial;
static int passed, failed;
#define CHECK(name, expression) do { int ok = !!(expression); passed += ok; failed += !ok; \
    printf("%s %s\n", ok ? "PASS" : "FAIL", name); \
    if (serial) { fprintf(serial, "%s %s\n", ok ? "PASS" : "FAIL", name); fflush(serial); } } while (0)

static int native_wifi(const char* action) {
    pid_t child=fork();if(child<0)return -1;
    if(!child){
        if(serial){dup2(fileno(serial),1);dup2(fileno(serial),2);}
        execl("/bin/wifi","wifi",action,(char*)0);_exit(127);
    }
    int status;while(waitpid(child,&status,0)<0)if(errno!=EINTR)return -1;
    return WIFEXITED(status)?WEXITSTATUS(status):-1;
}
int main(void) {
    serial = fopen("/dev/serial", "w");
    struct stat st={0};
    CHECK("firmware directory exists", stat("/lib/firmware", &st) == 0);
    CHECK("firmware blob present", stat("/lib/firmware/iwlwifi-cc-a0-71.ucode", &st) == 0);
    int size_ok = st.st_size > 1000000;
    CHECK("firmware blob size sane", size_ok);
    int fd = open("/lib/firmware/iwlwifi-cc-a0-71.ucode", O_RDONLY);
    CHECK("firmware blob opens", fd >= 0);
    if (fd >= 0) {
        uint8_t header[8];
        ssize_t n = read(fd, header, sizeof(header));
        CHECK("firmware header reads", n == 8 && header[0] == 0 && header[1] == 0 &&
              header[2] == 0 && header[3] == 0 && header[4] == 0x49 &&
              header[5] == 0x57 && header[6] == 0x4c && header[7] == 0x0a);
        close(fd);
    }
    char status[1024]={0};int status_fd=open("/proc/wifi",O_RDONLY);
    ssize_t status_n=status_fd>=0?read(status_fd,status,sizeof(status)-1):-1;
    if(status_fd>=0)close(status_fd);
    CHECK("driver status readable",status_n>0 && strstr(status,"stage: "));
    if(status_n>0){printf("%s",status);if(serial)fprintf(serial,"%s",status);}
    CHECK("native wifi status command",native_wifi("status")==0);
    int control=open("/dev/wifi0",O_RDWR);
    CHECK("WiFi control device opens",control>=0);
    if(control>=0){
        errno=0;CHECK("unknown WiFi control rejected",ioctl(control,0x4b57ff,0)==-1 && errno==ENOTTY);
        struct kwifi_join* invalid=mmap(0,4096,PROT_READ,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(invalid!=MAP_FAILED){errno=0;CHECK("read-only join input validates without writing",ioctl(control,KWIFI_JOIN,invalid)==-1 && errno==EINVAL);munmap(invalid,4096);}
        if(strstr(status,"stage: absent")){
            int job=ioctl(control,KWIFI_SCAN,0);CHECK("scan job queued",job>0);
            struct kwifi_result result={.job=job};
            for(int i=0;i<100 && !result.complete;i++){if(ioctl(control,KWIFI_RESULT,&result)<0)break;usleep(10000);}
            CHECK("absent scan completes with ENODEV",result.complete && result.error==-ENODEV);
            CHECK("native wifi scan reports absent hardware",native_wifi("scan")==1);
        }
        close(control);
    }
    int hardware_ready=strstr(status,"nvm-ready: 1")!=NULL;
    // This suite reports packaging and interface consistency. Successful
    // association, DHCP and traffic require wifi scan/join on real hardware.
    // Interface list always has lo; wlan0 appears with a live AX200.
    // QEMU has no AX200, so only lo is expected here.
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    CHECK("interface query socket", s >= 0);
    if (s >= 0) {
        char buffer[128] = {0};
        struct ifconf conf = { .ifc_len = sizeof(buffer), .ifc_buf = buffer };
        int has_lo = 0, has_wlan = 0;
        if (ioctl(s, SIOCGIFCONF, &conf) == 0) {
            for (size_t off = 0; off + 40 <= (size_t)conf.ifc_len; off += 40) {
                if (!memcmp(buffer + off, "lo", 3)) has_lo = 1;
                if (!memcmp(buffer + off, "wlan0", 6)) has_wlan = 1;
            }
        }
        CHECK("loopback interface listed", has_lo);
        CHECK("wlan0 matches driver readiness", !!has_wlan==!!hardware_ready);
        // Unknown wireless name is refused.
        struct ifreq req;
        memset(&req, 0, sizeof(req));
        memcpy(req.ifr_name, "wlan0", 6);
        CHECK("wlan0 flags match hardware readiness", (ioctl(s, SIOCGIFFLAGS, &req)==0)==hardware_ready);
        close(s);
    }
    printf("These checks do not verify AP association or internet access.\n");
    printf("wifitest: %d passed, %d failed\n", passed, failed);
    if (serial) { fprintf(serial, "wifitest: %d passed, %d failed\n", passed, failed); fclose(serial); }
    return failed ? 1 : 0;
}
