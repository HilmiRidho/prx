/* JIT: SSE2 shifts by immediate (psrlw/psraw/psllw/psrld/psrad/pslld), packuswb, pshufd with any immediate,
 * movntps/movntdq and the store form of movq, in a loop so the blocks get translated. */
#include <stdio.h>
#include <stdint.h>
typedef uint64_t u64;
static u64 seed = 0x9E3779B97F4A7C15ULL;
static inline __attribute__((always_inline)) u64 rnd(void) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed; }
static u64 sum;
static inline __attribute__((always_inline)) void mix(u64 v) { sum = sum * 1000003ULL + v; sum ^= sum >> 29; }
typedef struct { u64 q[2]; } v128 __attribute__((aligned(16)));
#define SH(ins, n) do { v128 a = { { rnd(), rnd() } }; \
    __asm__ volatile("movdqa %0, %%xmm0\n\t" ins " $" #n ", %%xmm0\n\tmovdqa %%xmm0, %0" : "+m"(a) :: "xmm0"); mix(a.q[0]); mix(a.q[1]); } while (0)
#define SHALL(n) SH("psrlw", n); SH("psraw", n); SH("psllw", n); SH("psrld", n); SH("psrad", n); SH("pslld", n)
#define SHUF(n) do { v128 a = { { rnd(), rnd() } }, b = { { rnd(), rnd() } }; \
    __asm__ volatile("movdqa %1, %%xmm0\n\tpshufd $" #n ", %%xmm0, %%xmm1\n\tmovdqa %%xmm1, %0\n\tpshufd $" #n ", %2, %%xmm2\n\tmovdqa %%xmm2, %1" : "=m"(a), "+m"(b) : "m"(b) : "xmm0", "xmm1", "xmm2"); \
    mix(a.q[0]); mix(a.q[1]); mix(b.q[0]); mix(b.q[1]); } while (0)
int main(void) {
    for (int i = 0; i < 300; i++) {
        SHALL(0); SHALL(1); SHALL(2); SHALL(7); SHALL(8); SHALL(15); SHALL(16); SHALL(17); SHALL(31); SHALL(32); SHALL(33); SHALL(63); SHALL(255);
        SHUF(0x1b); SHUF(0xe4); SHUF(0x4e); SHUF(0xb1); SHUF(0x39); SHUF(0x93); SHUF(0xc6); SHUF(0x27); SHUF(0x0a); SHUF(0xf0); SHUF(0x6c);
        v128 a = { { rnd(), rnd() } }, b = { { rnd(), rnd() } }, c, d;
        __asm__ volatile("movdqa %2, %%xmm0\n\tmovdqa %3, %%xmm1\n\tpackuswb %%xmm1, %%xmm0\n\tmovdqa %%xmm0, %0\n\tmovdqa %2, %%xmm0\n\tpackuswb %3, %%xmm0\n\tmovdqa %%xmm0, %1"
                         : "=m"(c), "=m"(d) : "m"(a), "m"(b) : "xmm0", "xmm1");
        mix(c.q[0]); mix(c.q[1]); mix(d.q[0]); mix(d.q[1]);
        v128 e = { { 1, 2 } }, f = { { 3, 4 } }, g = { { 5, 6 } };
        __asm__ volatile("movdqa %3, %%xmm0\n\tmovntdq %%xmm0, %0\n\tmovaps %3, %%xmm1\n\tmovntps %%xmm1, %1\n\tmovq %%xmm0, %2" : "=m"(e), "=m"(f), "=m"(g.q[1]) : "m"(a) : "xmm0", "xmm1");
        mix(e.q[0]); mix(e.q[1]); mix(f.q[0]); mix(f.q[1]); mix(g.q[0]); mix(g.q[1]);
    }
    printf("sse2b %016llx\n", (unsigned long long) sum);
    return 0;
}
