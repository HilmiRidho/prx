/* io_uring through raw syscalls: setup, mmap of the rings, a NOP and a READV, completion reaping. A host that refuses io_uring
 * (seccomp, Android, old kernel) makes the test exit with 77, which the harness reports as SKIP. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#include <linux/io_uring.h>

#define LD(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define ST(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)

int main(void) {
    struct io_uring_params p; memset(&p, 0, sizeof p);
    long fd = syscall(425, 8, &p);
    if (fd < 0) { int refused = errno == ENOSYS || errno == EPERM || errno == EACCES; printf("io_uring: %s\n", refused ? "refused by the host" : "error"); return refused ? 77 : 1; }   /* 77 = skip */
    size_t sqsz = p.sq_off.array + p.sq_entries * sizeof(unsigned), cqsz = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);
    if (p.features & IORING_FEAT_SINGLE_MMAP) { if (cqsz > sqsz) sqsz = cqsz; cqsz = sqsz; }
    char*sq = mmap(NULL, sqsz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, (int) fd, IORING_OFF_SQ_RING);
    char*cq = (p.features & IORING_FEAT_SINGLE_MMAP) ? sq : mmap(NULL, cqsz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, (int) fd, IORING_OFF_CQ_RING);
    struct io_uring_sqe*sqes = mmap(NULL, p.sq_entries * sizeof(struct io_uring_sqe), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, (int) fd, IORING_OFF_SQES);
    if (sq == MAP_FAILED || cq == MAP_FAILED || sqes == MAP_FAILED) { printf("io_uring: mmap failed\n"); return 1; }
    unsigned*stail = (unsigned*)(sq + p.sq_off.tail), *smask = (unsigned*)(sq + p.sq_off.ring_mask), *sarr = (unsigned*)(sq + p.sq_off.array);
    unsigned*chead = (unsigned*)(cq + p.cq_off.head), *ctail = (unsigned*)(cq + p.cq_off.tail), *cmask = (unsigned*)(cq + p.cq_off.ring_mask);
    struct io_uring_cqe*cqes = (struct io_uring_cqe*)(cq + p.cq_off.cqes);

    char path[] = "/tmp/pemu_uring_XXXXXX"; int f = mkstemp(path); unlink(path);
    if (write(f, "hello", 5) != 5) return 1;
    char buf[16] = {0}; struct iovec iv = { buf, sizeof buf };

    unsigned t = *stail;
    struct io_uring_sqe*e = &sqes[t & *smask]; memset(e, 0, sizeof *e); e->opcode = IORING_OP_NOP; e->user_data = 0x1234; sarr[t & *smask] = t & *smask; t++;
    e = &sqes[t & *smask]; memset(e, 0, sizeof *e); e->opcode = IORING_OP_READV; e->fd = f; e->addr = (unsigned long) &iv; e->len = 1; e->off = 0; e->user_data = 0x5678; sarr[t & *smask] = t & *smask; t++;
    ST(stail, t);
    long r = syscall(426, (int) fd, 2, 2, IORING_ENTER_GETEVENTS, NULL, 0);
    printf("enter: %ld\n", r);
    int got[2] = {0, 0}; int res[2] = {-1, -1};
    for (unsigned h = LD(chead); h != LD(ctail); h++) {
        struct io_uring_cqe*c = &cqes[h & *cmask];
        int i = c->user_data == 0x1234 ? 0 : c->user_data == 0x5678 ? 1 : -1;
        if (i >= 0) { got[i] = 1; res[i] = c->res; }
        ST(chead, h + 1);
    }
    printf("nop:   seen=%d res=%d\n", got[0], res[0]);
    printf("readv: seen=%d res=%d data=%s\n", got[1], res[1], buf);
    close((int) fd);
    return 0;
}
