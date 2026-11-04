/* MMX: every 0F opcode that exists on mm registers (and the SSSE3 ones), with register and memory operands, a spread of inputs,
 * and the moves to and from general registers and xmm. The expected output is what a real x86-64 prints. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static const uint64_t V[] = {
    0x0000000000000000ULL, 0xffffffffffffffffULL, 0x8000000080008080ULL, 0x7fff7fff7f7f7f7fULL, 0x0123456789abcdefULL,
    0xfedcba9876543210ULL, 0x00ff00ff00ff00ffULL, 0x0001000200030004ULL, 0x80ff7f01807f01ffULL,
};
#define NV (int) (sizeof V / sizeof V[0])
static uint64_t H;
static void out(const char *n, uint64_t a, uint64_t b, uint64_t r) { H = H * 1000003u + r; if (a == V[4] && b == V[5]) printf("%s %016llx\n", n, (unsigned long long) r); }

#define RR(op) do { for (int i = 0; i < NV; i++) for (int j = 0; j < NV; j++) { uint64_t r = V[i], b = V[j]; \
    __asm__ volatile("movq %0, %%mm0\n movq %1, %%mm1\n " #op " %%mm1, %%mm0\n movq %%mm0, %0\n emms" : "+m"(r) : "m"(b)); out(#op, V[i], V[j], r); } } while (0)
#define RM(op) do { for (int i = 0; i < NV; i++) for (int j = 0; j < NV; j++) { uint64_t r = V[i], b = V[j]; \
    __asm__ volatile("movq %0, %%mm0\n " #op " %1, %%mm0\n movq %%mm0, %0\n emms" : "+m"(r) : "m"(b)); out(#op "_m", V[i], V[j], r); } } while (0)
#define BOTH(op) RR(op); RM(op)
#define SH(op, n) do { for (int i = 0; i < NV; i++) { uint64_t r = V[i]; \
    __asm__ volatile("movq %0, %%mm0\n " #op " $" #n ", %%mm0\n movq %%mm0, %0\n emms" : "+m"(r)); out(#op "_" #n, V[i], 0, r); } } while (0)
#define SHW(n) SH(psllw, n); SH(pslld, n); SH(psllq, n); SH(psrlw, n); SH(psrld, n); SH(psrlq, n); SH(psraw, n); SH(psrad, n)
#define SHEW(op, n) do { for (int i = 0; i < NV; i++) { uint64_t r = V[i]; \
    __asm__ volatile("movq %0, %%mm0\n " #op " $" #n ", %1, %%mm0\n movq %%mm0, %0\n emms" : "+m"(r) : "m"(V[3])); out(#op "_" #n, V[i], 0, r); } } while (0)

int main(void) {
    BOTH(paddb); BOTH(paddw); BOTH(paddd); BOTH(paddq); BOTH(paddsb); BOTH(paddsw); BOTH(paddusb); BOTH(paddusw);
    BOTH(psubb); BOTH(psubw); BOTH(psubd); BOTH(psubq); BOTH(psubsb); BOTH(psubsw); BOTH(psubusb); BOTH(psubusw);
    BOTH(pmullw); BOTH(pmulhw); BOTH(pmulhuw); BOTH(pmaddwd); BOTH(pmuludq);
    BOTH(pand); BOTH(pandn); BOTH(por); BOTH(pxor);
    BOTH(pcmpeqb); BOTH(pcmpeqw); BOTH(pcmpeqd); BOTH(pcmpgtb); BOTH(pcmpgtw); BOTH(pcmpgtd);
    BOTH(packsswb); BOTH(packssdw); BOTH(packuswb);
    BOTH(punpcklbw); BOTH(punpcklwd); BOTH(punpckldq); BOTH(punpckhbw); BOTH(punpckhwd); BOTH(punpckhdq);
    BOTH(pavgb); BOTH(pavgw); BOTH(pmaxub); BOTH(pmaxsw); BOTH(pminub); BOTH(pminsw); BOTH(psadbw);
    BOTH(psllw); BOTH(pslld); BOTH(psllq); BOTH(psrlw); BOTH(psrld); BOTH(psrlq); BOTH(psraw); BOTH(psrad);
    BOTH(pshufb); BOTH(phaddw); BOTH(phaddd); BOTH(phaddsw); BOTH(phsubw); BOTH(phsubd); BOTH(phsubsw);
    BOTH(pmaddubsw); BOTH(pmulhrsw); BOTH(psignb); BOTH(psignw); BOTH(psignd);
    SHW(1); SHW(3); SHW(7); SHW(15); SHW(16); SHW(31); SHW(32); SHW(63); SHW(64);
    for (int i = 0; i < NV; i++) {                                       /* pabs: one source */
        uint64_t r;
        __asm__ volatile("movq %1, %%mm1\n pabsb %%mm1, %%mm0\n movq %%mm0, %0\n emms" : "=m"(r) : "m"(V[i])); out("pabsb", V[i], 0, r);
        __asm__ volatile("movq %1, %%mm1\n pabsw %%mm1, %%mm0\n movq %%mm0, %0\n emms" : "=m"(r) : "m"(V[i])); out("pabsw", V[i], 0, r);
        __asm__ volatile("pabsd %1, %%mm0\n movq %%mm0, %0\n emms" : "=m"(r) : "m"(V[i])); out("pabsd_m", V[i], 0, r);
    }
    for (int i = 0; i < NV; i++) for (int j = 0; j < NV; j++) {          /* imm8 forms */
        uint64_t a = V[i], b = V[j], r;
        __asm__ volatile("movq %1, %%mm0\n movq %2, %%mm1\n palignr $3, %%mm1, %%mm0\n movq %%mm0, %0\n emms" : "=m"(r) : "m"(a), "m"(b)); out("palignr3", a, b, r);
        __asm__ volatile("movq %1, %%mm0\n movq %2, %%mm1\n palignr $11, %%mm1, %%mm0\n movq %%mm0, %0\n emms" : "=m"(r) : "m"(a), "m"(b)); out("palignr11", a, b, r);
        __asm__ volatile("movq %1, %%mm0\n movq %2, %%mm1\n palignr $17, %%mm1, %%mm0\n movq %%mm0, %0\n emms" : "=m"(r) : "m"(a), "m"(b)); out("palignr17", a, b, r);
        __asm__ volatile("pshufw $0x1b, %1, %%mm0\n movq %%mm0, %0\n emms" : "=m"(r) : "m"(a)); out("pshufw1b", a, 0, r);
        __asm__ volatile("movq %1, %%mm1\n pshufw $0xc6, %%mm1, %%mm0\n movq %%mm0, %0\n emms" : "=m"(r) : "m"(a)); out("pshufwc6", a, 0, r);
    }
    for (int i = 0; i < NV; i++) {                                       /* mm <-> general registers, pextrw/pinsrw/pmovmskb, memory moves */
        uint64_t a = V[i], r; uint32_t d; uint64_t q;
        __asm__ volatile("movq %2, %%mm0\n movd %%mm0, %0\n movq %%mm0, %1\n emms" : "=r"(d), "=r"(q) : "m"(a)); out("movd_from", a, 0, d); out("movq_from", a, 0, q);
        __asm__ volatile("movd %1, %%mm0\n movq %%mm0, %0\n emms" : "=m"(r) : "r"((uint32_t) a)); out("movd_to", a, 0, r);
        __asm__ volatile("movq %1, %%mm0\n movq %%mm0, %0\n emms" : "=m"(r) : "r"(a)); out("movq_to", a, 0, r);
        __asm__ volatile("movq %1, %%mm0\n pextrw $2, %%mm0, %k0\n emms" : "=r"(d) : "m"(a)); out("pextrw2", a, 0, d);
        __asm__ volatile("movq %1, %%mm0\n pextrw $7, %%mm0, %k0\n emms" : "=r"(d) : "m"(a)); out("pextrw7", a, 0, d);
        r = a; __asm__ volatile("movq %0, %%mm0\n pinsrw $1, %1, %%mm0\n movq %%mm0, %0\n emms" : "+m"(r) : "r"((uint32_t) 0xbeef)); out("pinsrw1", a, 0, r);
        r = a; __asm__ volatile("movq %0, %%mm0\n pinsrw $6, %1, %%mm0\n movq %%mm0, %0\n emms" : "+m"(r) : "m"(V[5])); out("pinsrw6m", a, 0, r);
        __asm__ volatile("movq %1, %%mm0\n pmovmskb %%mm0, %k0\n emms" : "=r"(d) : "m"(a)); out("pmovmskb", a, 0, d);
        __asm__ volatile("movq %1, %%mm0\n movntq %%mm0, %0\n emms" : "=m"(r) : "m"(a)); out("movntq", a, 0, r);
        __asm__ volatile("movq %1, %%mm3\n movq %%mm3, %%mm5\n movq %%mm5, %0\n emms" : "=m"(r) : "m"(a)); out("movq_rr", a, 0, r);
        { __uint128_t x = ((__uint128_t) 0x1111111111111111ULL << 64) | 0x2222222222222222ULL; uint64_t m2;
          __asm__ volatile("movdqu %1, %%xmm4\n movdq2q %%xmm4, %%mm2\n movq %%mm2, %0\n emms" : "=m"(m2) : "m"(x)); out("movdq2q", a, 0, m2);
          __uint128_t y; __asm__ volatile("movq %1, %%mm1\n movq2dq %%mm1, %%xmm3\n movdqu %%xmm3, %0\n emms" : "=m"(y) : "m"(a)); out("movq2dq_lo", a, 0, (uint64_t) y); out("movq2dq_hi", a, 0, (uint64_t) (y >> 64)); }
        { uint8_t buf[8]; uint64_t mask = V[(i + 4) % NV], r8;          /* maskmovq stores the selected bytes at [rdi] */
          memset(buf, 0x55, 8);
          __asm__ volatile("movq %0, %%mm0\n movq %1, %%mm1\n maskmovq %%mm1, %%mm0\n emms" : : "m"(a), "m"(mask), "D"(buf) : "memory");
          memcpy(&r8, buf, 8); out("maskmovq", a, 0, r8); }
    }
    printf("hash %016llx\n", (unsigned long long) H);
    return 0;
}
