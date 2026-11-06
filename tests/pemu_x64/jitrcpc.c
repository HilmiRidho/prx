/* Multi-threaded guest (so the JIT emits x86 memory-ordering code) doing integer loads/stores of 2/4/8 bytes at every offset,
 * aligned and not, across 16/64-byte boundaries, plus push/pop/leave/ret on a misaligned rsp. With the ldapur/stlur translation
 * (JOPT_RCPC) a misaligned address must take the dmb path; the strict interpreter of the test build aborts if one reaches ldapur/stlur. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
typedef uint16_t __attribute__((aligned(1), may_alias)) u16u;
typedef uint32_t __attribute__((aligned(1), may_alias)) u32u;
typedef uint64_t __attribute__((aligned(1), may_alias)) u64u;
typedef int16_t  __attribute__((aligned(1), may_alias)) s16u;
typedef int32_t  __attribute__((aligned(1), may_alias)) s32u;

static volatile int stop;
static void *idle(void *a) { (void) a; while (!stop) usleep(1000); return 0; }
static unsigned char buf[256] __attribute__((aligned(64)));
static int bad;
#define CHECK(c) do { if (!(c)) { bad++; printf("FAIL line %d\n", __LINE__); } } while (0)

__asm__(".text\n.globl t_leave\nt_leave:\n push %rbp\n mov %rsp,%r10\n lea 3(%rdi),%rbp\n leave\n mov %rbp,%rax\n mov %r10,%rsp\n pop %rbp\n ret\n"
        ".globl t_ret\nt_ret:\n mov %rsp,%r10\n lea 3(%rdi),%rsp\n ret\n"
        ".globl t_land\nt_land:\n mov %r10,%rsp\n mov $77,%eax\n ret\n");
extern uint64_t t_leave(void *p), t_ret(void *p);
extern char t_land[];

static uint64_t pushpop(uint64_t v, void *where) {
    uint64_t out;
    __asm__ volatile("mov %%rsp,%%r11\n lea 3(%2),%%rsp\n push %1\n pop %0\n mov %%r11,%%rsp" : "=r"(out) : "r"(v), "r"(where) : "r11", "memory");
    return out;
}

int main(void) {
    pthread_t t; pthread_create(&t, 0, idle, 0);
    for (int off = 0; off < 40; off++) {
        unsigned char *p = buf + 16 + off;
        uint64_t v = 0x1122334455667788ull ^ ((uint64_t) off * 0x0101010101010101ull);
        memset(buf, 0xA5, sizeof buf);
        *(volatile u64u *) p = v;
        for (int i = 0; i < 8; i++) CHECK(p[i] == (unsigned char)(v >> (8 * i)));
        CHECK(*(volatile u64u *) p == v);
        CHECK(buf[16 + off - 1] == 0xA5 && buf[16 + off + 8] == 0xA5);
        *(volatile u32u *) (p + 1) = (uint32_t) (v >> 7);
        CHECK(*(volatile u32u *) (p + 1) == (uint32_t) (v >> 7));
        *(volatile u16u *) (p + 3) = (uint16_t) (v >> 3);
        CHECK(*(volatile u16u *) (p + 3) == (uint16_t) (v >> 3));
        for (int i = 0; i < 8; i++) p[i] = (unsigned char) (0x80 + i + off);        /* byte stores, then wide loads */
        uint64_t w = 0; for (int i = 7; i >= 0; i--) w = w << 8 | (unsigned char) (0x80 + i + off);
        CHECK(*(volatile u64u *) p == w);
        CHECK((int64_t) *(volatile s32u *) p == (int64_t) (int32_t) (uint32_t) w);         /* movsxd */
        CHECK((int64_t) *(volatile s16u *) (p + 2) == (int64_t) (int16_t) (uint16_t) (w >> 16));   /* movsx */
        *(volatile u64u *) p += 5; CHECK(*(volatile u64u *) p == w + 5);                    /* read-modify-write */
    }
    for (int off = 0; off < 16; off++) {
        uint64_t v = 0xDEADBEEF00000000ull | (uint64_t) off;
        memset(buf, 0, sizeof buf);
        CHECK(pushpop(v, buf + 64 + off) == v);
        *(volatile u64u *) (buf + 3 + off) = v;
        CHECK(t_leave(buf + off) == v);
        *(volatile u64u *) (buf + 3 + off) = (uint64_t) t_land;
        CHECK(t_ret(buf + off) == 77);
    }
    stop = 1; pthread_join(t, 0);
    puts(bad ? "rcpc FAIL" : "rcpc ok");
    return bad != 0;
}
