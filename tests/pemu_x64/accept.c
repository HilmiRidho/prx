/* accept(2) as a raw syscall: the fourth argument register is garbage for accept, so it must not reach the host as accept4 flags. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/syscall.h>

int main(void) {
    struct sockaddr_un sa; memset(&sa, 0, sizeof sa); sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof sa.sun_path, "/tmp/pemu_accept_%d", (int) getpid());
    int ls = socket(AF_UNIX, SOCK_STREAM, 0), cs = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ls < 0 || cs < 0 || bind(ls, (struct sockaddr*) &sa, sizeof sa) || listen(ls, 1) || connect(cs, (struct sockaddr*) &sa, sizeof sa)) { puts("setup FAIL"); return 1; }
    struct sockaddr_un pa; socklen_t pl = sizeof pa;
    long fd = syscall(SYS_accept, ls, &pa, &pl, 0x7fffffffL, 0L, 0L);
    printf("accept: %s\n", fd >= 0 ? "ok" : "FAIL");
    if (fd >= 0) {
        char b[8] = { 0 };
        write(cs, "hi", 2);
        printf("data: %s\n", read((int) fd, b, sizeof b) == 2 && !memcmp(b, "hi", 2) ? "ok" : "FAIL");
        close((int) fd);
    }
    errno = 0; long r = syscall(SYS_accept, 999, 0L, 0L, 0L, 0L, 0L);
    printf("bad fd: %s\n", r == -1 && errno == EBADF ? "ok" : "FAIL");
    close(cs); close(ls); unlink(sa.sun_path);
    return 0;
}
