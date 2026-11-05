/* JIT: MMX movd/movq (register and memory forms, 32 and 64 bit), movq mm,mm/m64, pshufw with several immediates,
 * lane-wise MMX arithmetic and logic and emms, in a loop so the blocks get translated. */
#include <stdio.h>
#include <stdint.h>
typedef uint64_t u64; typedef uint32_t u32;
static u64 seed = 0xD1B54A32D192ED03ULL;
static inline __attribute__((always_inline)) u64 rnd(void) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed; }
static u64 sum;
static inline __attribute__((always_inline)) void mix(u64 v) { sum = sum * 1000003ULL + v; sum ^= sum >> 29; }
#define ARITH(ins) do { u64 a = rnd(), b = rnd(), r, r2; \
    __asm__ volatile("movq %2, %%mm0\n\tmovq %3, %%mm1\n\t" ins " %%mm1, %%mm0\n\tmovq %%mm0, %0\n\tmovq %2, %%mm2\n\t" ins " %3, %%mm2\n\tmovq %%mm2, %1\n\temms" \
                     : "=m"(r), "=m"(r2) : "m"(a), "m"(b) : "mm0", "mm1", "mm2"); mix(r); mix(r2); } while (0)
#define SHUFW(n) do { u64 a = rnd(), b = rnd(), r, r2; \
    __asm__ volatile("movq %2, %%mm0\n\tpshufw $" #n ", %%mm0, %%mm1\n\tmovq %%mm1, %0\n\tpshufw $" #n ", %3, %%mm2\n\tmovq %%mm2, %1\n\temms" \
                     : "=m"(r), "=m"(r2) : "m"(a), "m"(b) : "mm0", "mm1", "mm2"); mix(r); mix(r2); } while (0)
int main(void) {
    for (int i = 0; i < 1500; i++) {
        u64 x = rnd(), y = rnd(), z, w; u32 lo, lo2;
        __asm__ volatile("movd %k3, %%mm0\n\tmovd %%mm0, %k0\n\tmovq %4, %%mm1\n\tmovd %%mm1, %1\n\tmovq %%mm1, %2\n\temms" : "=r"(lo), "=m"(lo2), "=m"(z) : "r"(x), "m"(y) : "mm0", "mm1");
        mix(lo); mix(lo2); mix(z);
        __asm__ volatile("movq %2, %%mm3\n\tmovd %3, %%mm4\n\tmovq %%mm3, %%mm5\n\tmovq %%mm5, %0\n\tmovq %%mm4, %1\n\temms" : "=m"(z), "=m"(w) : "m"(y), "m"(lo2) : "mm3", "mm4", "mm5");
        mix(z); mix(w);
        u64 q = rnd(), q2;
        __asm__ volatile("movq %1, %%mm6\n\tmovq %%mm6, %%rax\n\tmovq %%rax, %%mm7\n\tmovq %%mm7, %0\n\temms" : "=m"(q2) : "m"(q) : "mm6", "mm7", "rax");
        mix(q2);
        ARITH("paddb"); ARITH("paddw"); ARITH("paddd"); ARITH("psubb"); ARITH("psubw"); ARITH("psubd"); ARITH("pand"); ARITH("pandn"); ARITH("por"); ARITH("pxor");
        ARITH("pcmpeqb"); ARITH("pcmpeqw"); ARITH("pcmpeqd"); ARITH("pcmpgtb"); ARITH("pcmpgtw"); ARITH("pcmpgtd"); ARITH("pmullw");
        ARITH("paddusb"); ARITH("psubusw"); ARITH("paddsw"); ARITH("pminub"); ARITH("pmaxsw"); ARITH("pavgb");
        ARITH("punpcklbw"); ARITH("punpckhwd"); ARITH("packuswb");
        SHUFW(0x1b); SHUFW(0xe4); SHUFW(0x00); SHUFW(0xaa); SHUFW(0x4e); SHUFW(0xc6);
    }
    printf("mmx %016llx\n", (unsigned long long) sum);
    return 0;
}
