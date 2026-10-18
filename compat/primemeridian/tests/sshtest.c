// This file has been generated in whole or in part by a Large Language Model
// Disposable live-image SSH checks, coordinated by buildutils/ssh_test.py.
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
        fd = connect_to(18082, inet_addr("10.0.2.2"));
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
static int marker_present(void) {
    char marker[16] = {0};
    int fd = open("/tmp/ssh-inbound-ok", O_RDONLY);
    if (fd < 0) return 0;
    ssize_t n = read(fd, marker, sizeof(marker)); close(fd);
    return n == 10 && !memcmp(marker, "inbound-ok", 10);
}
static int entropy_devices(void) {
    unsigned char bytes[16];
    if (getrandom(bytes, sizeof(bytes), 0) != sizeof(bytes)) return 0;
    const char* devices[] = { "/dev/random", "/dev/urandom" };
    for (unsigned i = 0; i < 2; i++) {
        struct stat st;
        int fd = open(devices[i], O_RDONLY);
        if (fd < 0) return 0;
        int ok = !fstat(fd, &st) && S_ISCHR(st.st_mode) && (st.st_mode & 0777) == 0444 && read(fd, bytes, sizeof(bytes)) == sizeof(bytes);
        close(fd);
        if (!ok) return 0;
    }
    return 1;
}
// These three programs write diagnostics and public keys only. Never dump
// hostkey, client.key or authorized_keys; credential bytes stay in the RAM VM.
static void dump_failure_logs(void) {
    static const char* const paths[] = {
        "/tmp/ssh-keygen.log", "/tmp/ssh-server.log", "/tmp/ssh-client.log"
    };
    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        int fd = open(paths[i], O_RDONLY);
        if (fd < 0) continue;
        char bytes[2048];
        ssize_t n;
        do { n = read(fd, bytes, sizeof(bytes)); } while (n < 0 && errno == EINTR);
        close(fd);
        if (n <= 0) continue;
        printf("sshtest diagnostic %s (at most 2048 bytes):\n", paths[i]);
        (void)fwrite(bytes, 1, (size_t)n, stdout);
        printf("\n");
        if (serial) {
            fprintf(serial, "sshtest diagnostic %s (at most 2048 bytes):\n", paths[i]);
            (void)fwrite(bytes, 1, (size_t)n, serial);
            fprintf(serial, "\n");
            fflush(serial);
        }
    }
}
int main(int argc, char** argv) {
    serial = fopen("/dev/serial", "w");
    pid_t server = -1;
    char* end = NULL;
    unsigned long port = argc == 2 ? strtoul(argv[1], &end, 10) : 0;
    CHECK("SSH root and forwarding port", getuid() == 0 && port >= 1024 && port <= 65535 && end && !*end);
    if (failed) goto done;
    CHECK("SSH hardware entropy and read-only devices", entropy_devices());
    if (failed) goto done;
    struct passwd* root = getpwnam("root");
    char directory[512], authorized[544];
    int n = root && root->pw_dir ? snprintf(directory, sizeof(directory), "%s/.ssh", root->pw_dir) : -1;
    int dirs_ok = n > 0 && n < (int)sizeof(directory);
    if (dirs_ok) {
        dirs_ok = (mkdir(root->pw_dir, 0700) == 0 || errno == EEXIST) &&
                  (mkdir(directory, 0700) == 0 || errno == EEXIST) && chmod(directory, 0700) == 0;
        snprintf(authorized, sizeof(authorized), "%s/authorized_keys", directory);
    }
    CHECK("SSH root credential directory", dirs_ok);
    if (failed) goto done;
    CHECK("SSH disposable HTTP credentials", fetch("/authorized_keys", authorized) == 0 && fetch("/client.key", "/tmp/client.key") == 0);
    if (failed) goto done;
    char* keygen[] = { "/bin/dropbearkey", "-t", "ed25519", "-f", "/tmp/hostkey", NULL };
    pid_t child = launch(keygen, "/tmp/ssh-keygen.log");
    CHECK("SSH guest host key generation", child > 0 && wait_child(child, 30) == 0);
    if (failed) goto done;
    unlink("/tmp/ssh-inbound-ok");
    char* daemon[] = { "/bin/dropbear", "-F", "-s", "-p", "2222", "-r", "/tmp/hostkey", NULL };
    server = launch(daemon, "/dev/serial");
    int listening = 0;
    long long deadline = milliseconds() + 15000;
    while (server > 0 && milliseconds() < deadline) {
        int fd = connect_to(2222, htonl(INADDR_LOOPBACK));
        if (fd >= 0) { close(fd); listening = 1; break; }
        usleep(100000);
    }
    CHECK("SSH explicit server startup", listening);
    if (failed) goto done;
    printf("sshtest: server ready\n"); fflush(stdout);
    if (serial) { fprintf(serial, "sshtest: server ready\n"); fflush(serial); }
    deadline = milliseconds() + 60000;
    while (!marker_present() && milliseconds() < deadline) usleep(100000);
    CHECK("SSH OpenSSH inbound authenticated command", marker_present());
    if (failed) goto done;
    char port_text[8]; snprintf(port_text, sizeof(port_text), "%lu", port);
    char* client[] = { "/bin/dbclient", "-T", "-y", "-i", "/tmp/client.key", "-p", port_text,
                      "root@10.0.2.2", "echo outbound-ok", NULL };
    child = launch(client, "/tmp/ssh-client.log");
    int status = child > 0 ? wait_child(child, 45) : -1;
    char output[4096] = {0};
    int fd = open("/tmp/ssh-client.log", O_RDONLY);
    if (fd >= 0) { (void)read(fd, output, sizeof(output) - 1); close(fd); }
    CHECK("SSH dbclient outbound authenticated command", status == 0 && strstr(output, "outbound-ok\n") != NULL);
done:
    if (failed) dump_failure_logs();
    if (server > 0) { kill(server, SIGTERM); (void)wait_child(server, 3); }
    printf("sshtest: %d passed, %d failed\n", passed, failed);
    if (serial) { fprintf(serial, "sshtest: %d passed, %d failed\n", passed, failed); fclose(serial); }
    return failed ? 1 : 0;
}
