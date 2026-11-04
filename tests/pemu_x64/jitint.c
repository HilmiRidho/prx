/* JIT integer paths: one-operand mul/imul, bswap, 8/16-bit shifts, and jcc/setcc/cmov after interpreter fallbacks
 * (bt, adc, sbb). Every group runs in a loop so the blocks get translated, with the flags dead and live. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef uint64_t u64; typedef uint32_t u32; typedef uint16_t u16; typedef uint8_t u8;
static u64 seed = 0x9E3779B97F4A7C15ULL;
static inline __attribute__((always_inline)) u64 rnd(void) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed; }
static u64 edge(int i) { static const u64 e[] = { 0, 1, 2, 0x7fffffffULL, 0x80000000ULL, 0xffffffffULL, 0x7fffffffffffffffULL, 0x8000000000000000ULL, ~0ULL, 0x100000000ULL, 10, 0xcccccccdULL }; return i < 12 ? e[i] : rnd(); }
static u64 sum;
static inline __attribute__((always_inline)) void mix(u64 v) { sum = sum * 1000003ULL + v; sum ^= sum >> 29; }

int main(void) {
    for (int i = 0; i < 400; i++) {
        u64 a = edge(i % 20), b = edge((i / 20) % 20 + (i & 1)), lo, hi, fl;
        u32 a32 = (u32) a, b32 = (u32) b, lo32, hi32;
        /* mul / imul, 64 and 32 bit, flags dead */
        asm("mulq %3" : "=a"(lo), "=d"(hi) : "0"(a), "r"(b) : "cc"); mix(lo); mix(hi);
        asm("imulq %3" : "=a"(lo), "=d"(hi) : "0"(a), "r"(b) : "cc"); mix(lo); mix(hi);
        asm("mull %3" : "=a"(lo32), "=d"(hi32) : "0"(a32), "r"(b32) : "cc"); mix(lo32); mix(hi32);
        asm("imull %3" : "=a"(lo32), "=d"(hi32) : "0"(a32), "r"(b32) : "cc"); mix(lo32); mix(hi32);
        asm("mulq %3" : "=a"(lo), "=d"(hi) : "0"(a), "m"(b) : "cc"); mix(lo); mix(hi);
        asm("imull %3" : "=a"(lo32), "=d"(hi32) : "0"(a32), "m"(b32) : "cc"); mix(lo32); mix(hi32);
        asm("mulq %%rdx" : "=a"(lo), "=d"(hi) : "0"(a), "1"(b) : "cc"); mix(lo); mix(hi);      /* source is rdx */
        asm("mull %%eax" : "=a"(lo32), "=d"(hi32) : "0"(a32) : "cc"); mix(lo32); mix(hi32);    /* source is rax */
        /* flags live: CF and OF as the processor leaves them */
        asm("mulq %4; setc %b2; seto %b3" : "=a"(lo), "=d"(hi), "=r"(fl), "=r"(b) : "r"(b), "0"(a) : "cc"); mix(lo); mix(hi); mix(fl & 1); mix(b & 1);
        asm("imulq %4; setc %b2; seto %b3" : "=a"(lo), "=d"(hi), "=r"(fl), "=r"(b) : "r"(b), "0"(a) : "cc"); mix(lo); mix(hi); mix(fl & 1); mix(b & 1);
        /* bswap */
        u64 x = a ^ (b << 7);
        asm("bswapq %0" : "+r"(x)); mix(x);
        u32 y = (u32)(a ^ b);
        asm("bswapl %0" : "+r"(y)); mix(y);
        register u64 r9v asm("r9") = a; asm("bswapq %%r9" : "+r"(r9v)); mix(r9v);
        register u32 r12v asm("r12") = b32; asm("bswapl %%r12d" : "+r"(r12v)); mix(r12v);
        /* 8/16-bit shifts, flags dead */
        u16 w = (u16)(a >> 3); u8 c = (u8)(b & 31); u8 q = (u8)(a >> 9);
        u16 w1 = w, w2 = w, w3 = w; u8 q1 = q, q2 = q, q3 = q;
        asm("shlw %%cl, %0" : "+r"(w1) : "c"(c) : "cc");
        asm("shrw %%cl, %0" : "+r"(w2) : "c"(c) : "cc");
        asm("sarw %%cl, %0" : "+r"(w3) : "c"(c) : "cc");
        asm("shlb %%cl, %0" : "+q"(q1) : "c"(c) : "cc");
        asm("shrb %%cl, %0" : "+q"(q2) : "c"(c) : "cc");
        asm("sarb %%cl, %0" : "+q"(q3) : "c"(c) : "cc");
        mix(w1); mix(w2); mix(w3); mix(q1); mix(q2); mix(q3);
        w1 = w; w2 = w; w3 = w; q1 = q; q2 = q; q3 = q;
        asm("shlw $5, %0" : "+r"(w1) : : "cc"); asm("shrw $3, %0" : "+r"(w2) : : "cc"); asm("sarw $7, %0" : "+r"(w3) : : "cc");
        asm("shlb $3, %0" : "+q"(q1) : : "cc"); asm("shrb $2, %0" : "+q"(q2) : : "cc"); asm("sarb $1, %0" : "+q"(q3) : : "cc");
        mix(w1); mix(w2); mix(w3); mix(q1); mix(q2); mix(q3);
        u16 mw = w; u8 mb = q;
        asm("sarw %%cl, %0" : "+m"(mw) : "c"(c) : "cc"); asm("shlb $4, %0" : "+m"(mb) : : "cc"); mix(mw); mix(mb);
        { u32 e = (u32) a & 0xff00; asm("sarb $2, %%ah" : "+a"(e) : : "cc"); mix(e); }       /* ah */
        /* 8/16-bit shifts, flags live (CF/ZF/SF after) */
        w1 = w; q1 = q; u8 f1, f2, f3;
        asm("shlw %%cl, %0; setc %1; setz %2; sets %3" : "+r"(w1), "=r"(f1), "=r"(f2), "=r"(f3) : "c"(c) : "cc"); mix(w1); mix(f1); mix(f2); mix(f3);
        asm("shrb $1, %0; setc %1; setz %2" : "+q"(q1), "=q"(f1), "=q"(f2) : : "cc"); mix(q1); mix(f1); mix(f2);
        /* jcc / setcc / cmov after bt, adc, sbb (interpreted: flags are in memory) */
        u64 bit = b & 63, v = a, t1 = 0, t2 = 7, t3 = 9, t4;
        asm("bt %2, %1; setc %b0" : "=q"(t1) : "r"(v), "r"(bit) : "cc"); mix(t1 & 1);
        asm("bt %2, %1; cmovc %3, %0" : "+r"(t2) : "r"(v), "r"(bit), "r"(t3) : "cc"); mix(t2);
        asm("bt %2, %1; cmovae %3, %0" : "+r"(t3) : "r"(v), "r"(bit), "r"(t2) : "cc"); mix(t3);
        t4 = 0; asm("bt %2, %1; jnc 1f; mov $1, %0; 1:" : "+r"(t4) : "r"(v), "r"(bit) : "cc"); mix(t4);
        t4 = 0; asm("bt $5, %1; jc 1f; add $3, %0; 1: add $4, %0" : "+r"(t4) : "r"(v) : "cc"); mix(t4);
        u64 lo2 = a + b, c1 = lo2 < a, d1 = 0;
        asm("add %3, %1; adc %4, %2; setc %b0" : "=q"(c1), "+r"(a), "+r"(d1) : "r"(b), "r"(b >> 1) : "cc"); mix(c1 & 1); mix(a); mix(d1);
        u64 s1 = b, s2 = 0;
        asm("sub %2, %1; sbb $0, %0" : "+r"(s2), "+r"(s1) : "r"(a) : "cc"); mix(s2); mix(s1);
        u64 s3 = 5; asm("cmp %2, %1; sbb %0, %0" : "+r"(s3) : "r"(a), "r"(b) : "cc"); mix(s3);
        u64 s4 = 1; asm("add %2, %1; adc $0, %0; jc 1f; add $100, %0; 1:" : "+r"(s4), "+r"(a) : "r"(b) : "cc"); mix(s4);
        u32 s5 = 0; asm("stc; adc $0, %0; jnz 1f; add $5, %0; 1:" : "+r"(s5) : : "cc"); mix(s5);
        /* division by a constant: one-operand mul in compiled code */
        mix(a / 10); mix(b / 1000003ULL); mix((u32) a32 / 7u); mix((int64_t) b / -9);
    }
    printf("jitint %016llx\n", (unsigned long long) sum);
    return 0;
}
