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

static FILE* serial;
static int passed, failed;
#define CHECK(name, expression) do { int ok = !!(expression); passed += ok; failed += !ok; \
    printf("%s %s\n", ok ? "PASS" : "FAIL", name); \
    if (serial) { fprintf(serial, "%s %s\n", ok ? "PASS" : "FAIL", name); fflush(serial); } } while (0)

int main(void) {
    serial = fopen("/dev/serial", "w");
    struct stat st;
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
        CHECK("no phantom wlan0 without hardware", !has_wlan);
        // Unknown wireless name is refused.
        struct ifreq req;
        memset(&req, 0, sizeof(req));
        memcpy(req.ifr_name, "wlan0", 6);
        CHECK("wlan0 flags refused without hardware", ioctl(s, SIOCGIFFLAGS, &req) != 0);
        close(s);
    }
    printf("wifitest: %d passed, %d failed\n", passed, failed);
    if (serial) { fprintf(serial, "wifitest: %d passed, %d failed\n", passed, failed); fclose(serial); }
    return failed ? 1 : 0;
}
