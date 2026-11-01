/* epoll_ctl / epoll_wait with a data.u64 that must survive the round trip.
 * The guest's struct epoll_event is packed (12 bytes); on an aarch64 host the kernel's is 16 bytes, so epoll_ctl needs
 * converting just like epoll_wait. The bytes after the event are poisoned so that a wrong read shows up in the output. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>

struct slot { struct epoll_event ev; uint64_t poison; };

int main(void) {
    int p[2], ep = epoll_create1(0);
    if (ep < 0 || pipe(p) < 0) { puts("setup failed"); return 1; }
    struct slot s;
    memset(&s, 0xA5, sizeof s);
    s.ev.events = EPOLLIN;
    s.ev.data.u64 = 0x1122334455667788ULL;
    if (epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &s.ev) < 0) { puts("ADD failed"); return 1; }
    if (write(p[1], "x", 1) != 1) return 1;
    struct epoll_event out[4]; memset(out, 0, sizeof out);
    int n = epoll_wait(ep, out, 4, 1000);
    printf("n=%d events=%#x data=%#llx\n", n, n > 0 ? out[0].events : 0, n > 0 ? (unsigned long long) out[0].data.u64 : 0ULL);

    memset(&s, 0x5A, sizeof s);
    s.ev.events = EPOLLIN;
    s.ev.data.u64 = 0xCAFEBABE00C0FFEEULL;
    if (epoll_ctl(ep, EPOLL_CTL_MOD, p[0], &s.ev) < 0) { puts("MOD failed"); return 1; }
    memset(out, 0, sizeof out);
    n = epoll_wait(ep, out, 4, 1000);
    printf("n=%d events=%#x data=%#llx\n", n, n > 0 ? out[0].events : 0, n > 0 ? (unsigned long long) out[0].data.u64 : 0ULL);

    printf("del=%d\n", epoll_ctl(ep, EPOLL_CTL_DEL, p[0], NULL));
    return 0;
}
