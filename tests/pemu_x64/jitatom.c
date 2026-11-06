/* JIT: lock-prefixed add/sub/and/or/xor/inc/dec, cmpxchg, xadd, xchg (aligned and misaligned), adc/sbb, shifts by cl with live flags
 * (a zero count keeps the old flags), push m64, xchg rax,r, movq xmm,xmm/m64 and 8/16-bit rol/ror. Every group runs in a loop
 * so the blocks are translated, with the flags live (captured with pushfq) and dead. The output is a checksum: it must be the
 * same natively, in the interpreter (PEMU_JIT_OPT=0) and in the JIT. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef uint64_t u64; typedef uint32_t u32; typedef uint16_t u16; typedef uint8_t u8;
static u64 seed = 0x9E3779B97F4A7C15ULL;
static inline __attribute__((always_inline)) u64 rnd(void) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed; }
static u64 edge(int i) { static const u64 e[] = { 0, 1, 2, 0x7fULL, 0x80ULL, 0xffULL, 0x7fffULL, 0x8000ULL, 0xffffULL, 0x7fffffffULL, 0x80000000ULL, 0xffffffffULL,
    0x7fffffffffffffffULL, 0x8000000000000000ULL, ~0ULL, 0x100000000ULL }; return i < 16 ? e[i] : rnd(); }
static u64 sum;
static inline __attribute__((always_inline)) void mix(u64 v) { sum = sum * 1000003ULL + v; sum ^= sum >> 29; }
#define FM 0x8c5ULL            /* CF PF ZF SF OF */
static u8 buf[256] __attribute__((aligned(64)));

#define LOCKOP(ins, sfx, T, reg, A, B, off) do { T *p_ = (T*)(buf + (off)); *p_ = (T)(A); u64 f_; \
    asm volatile("lock " ins sfx " %[s], %[m]; pushfq; popq %[f]" : [m]"+m"(*p_), [f]"=r"(f_) : [s]reg((T)(B)) : "cc", "memory"); \
    mix(*p_); mix(f_ & FM); \
    *p_ = (T)(A); asm volatile("lock " ins sfx " %[s], %[m]; cmpl %k[f], %k[f]" : [m]"+m"(*p_), [f]"=r"(f_) : [s]reg((T)(B)) : "cc", "memory"); mix(*p_); } while (0)
#define LOCKIMM(ins, sfx, T, A, off) do { T *p_ = (T*)(buf + (off)); *p_ = (T)(A); u64 f_; \
    asm volatile("lock " ins sfx " $0x35, %[m]; pushfq; popq %[f]" : [m]"+m"(*p_), [f]"=r"(f_) : : "cc", "memory"); mix(*p_); mix(f_ & FM); } while (0)
#define LOCKID(ins, sfx, T, A, off, cf) do { T *p_ = (T*)(buf + (off)); *p_ = (T)(A); u64 f_; \
    asm volatile(cf "; lock " ins sfx " %[m]; pushfq; popq %[f]" : [m]"+m"(*p_), [f]"=r"(f_) : : "cc", "memory"); mix(*p_); mix(f_ & FM); } while (0)
#define CMPXCHG(sfx, T, areg, sreg, A, M, N, off) do { T *p_ = (T*)(buf + (off)); *p_ = (T)(M); T acc_ = (T)(A); u64 f_; \
    asm volatile("lock cmpxchg" sfx " %[n], %[m]; pushfq; popq %[f]" : [m]"+m"(*p_), "+" areg(acc_), [f]"=r"(f_) : [n]sreg((T)(N)) : "cc", "memory"); \
    mix(*p_); mix(acc_); mix(f_ & FM); \
    *p_ = (T)(M); acc_ = (T)(A); asm volatile("cmpxchg" sfx " %[n], %[m]; cmpl %k[f], %k[f]" : [m]"+m"(*p_), "+" areg(acc_), [f]"=r"(f_) : [n]sreg((T)(N)) : "cc", "memory"); mix(*p_); mix(acc_); } while (0)
#define XADD(sfx, T, reg, A, B, off) do { T *p_ = (T*)(buf + (off)); *p_ = (T)(A); T r_ = (T)(B); u64 f_; \
    asm volatile("lock xadd" sfx " %[r], %[m]; pushfq; popq %[f]" : [m]"+m"(*p_), [r]"+" reg(r_), [f]"=r"(f_) : : "cc", "memory"); mix(*p_); mix(r_); mix(f_ & FM); } while (0)
#define XCHG(sfx, T, reg, A, B, off) do { T *p_ = (T*)(buf + (off)); *p_ = (T)(A); T r_ = (T)(B); \
    asm volatile("xchg" sfx " %[r], %[m]" : [m]"+m"(*p_), [r]"+" reg(r_) : : "memory"); mix(*p_); mix(r_); } while (0)

int main(void) {
    for (int i = 0; i < 400; i++) {
        u64 a = edge(i % 20), b = edge((i / 20) % 20 + (i & 1)), f;
        for (int off = 0; off < 10; off += (off == 0 ? 1 : 3)) {      /* offsets 0 (aligned) and 1, 4, 7: misaligned for the wider sizes */
            LOCKOP("add", "q", u64, "r", a, b, off); LOCKOP("add", "l", u32, "r", a, b, off); LOCKOP("add", "w", u16, "r", a, b, off); LOCKOP("add", "b", u8, "q", a, b, off);
            LOCKOP("sub", "q", u64, "r", a, b, off); LOCKOP("sub", "l", u32, "r", a, b, off); LOCKOP("sub", "w", u16, "r", a, b, off); LOCKOP("sub", "b", u8, "q", a, b, off);
            LOCKOP("and", "q", u64, "r", a, b, off); LOCKOP("or", "l", u32, "r", a, b, off); LOCKOP("xor", "w", u16, "r", a, b, off); LOCKOP("xor", "b", u8, "q", a, b, off);
            LOCKIMM("add", "q", u64, a, off); LOCKIMM("sub", "l", u32, a, off); LOCKIMM("and", "w", u16, a, off); LOCKIMM("or", "b", u8, a, off); LOCKIMM("xor", "q", u64, a, off);
            LOCKID("inc", "q", u64, a, off, "stc"); LOCKID("inc", "l", u32, a, off, "clc"); LOCKID("dec", "q", u64, a, off, "stc"); LOCKID("dec", "l", u32, a, off, "clc");
            LOCKID("inc", "w", u16, a, off, "stc"); LOCKID("dec", "b", u8, a, off, "clc");
            CMPXCHG("q", u64, "a", "r", a, (i & 1) ? a : b, edge(i % 7), off); CMPXCHG("l", u32, "a", "r", a, (i & 2) ? a : b, b, off);
            CMPXCHG("w", u16, "a", "r", a, (i & 1) ? a : b, b, off); CMPXCHG("b", u8, "a", "q", a, (i & 2) ? a : b, b, off);
            XADD("q", u64, "r", a, b, off); XADD("l", u32, "r", a, b, off); XADD("w", u16, "r", a, b, off); XADD("b", u8, "q", a, b, off);
            XCHG("q", u64, "r", a, b, off); XCHG("l", u32, "r", a, b, off); XCHG("w", u16, "r", a, b, off); XCHG("b", u8, "q", a, b, off);
        }
        /* adc / sbb: the carry in from stc/clc and from a previous add/sub, flags live and dead, register, memory and immediate forms */
        for (int cf = 0; cf < 2; cf++) {
            u64 d = a, s = b, m = b, fl; u32 d32 = (u32) a, s32 = (u32) b;
            asm volatile("btl $0, %k[c]; adcq %[s], %[d]; pushfq; popq %[f]" : [d]"+r"(d), [f]"=r"(fl) : [s]"r"(s), [c]"r"((u32) cf) : "cc"); mix(d); mix(fl & FM);
            d = a; asm volatile("btl $0, %k[c]; sbbq %[s], %[d]; pushfq; popq %[f]" : [d]"+r"(d), [f]"=r"(fl) : [s]"r"(s), [c]"r"((u32) cf) : "cc"); mix(d); mix(fl & FM);
            asm volatile("btl $0, %k[c]; adcl %[s], %[d]; pushfq; popq %[f]" : [d]"+r"(d32), [f]"=r"(fl) : [s]"r"(s32), [c]"r"((u32) cf) : "cc"); mix(d32); mix(fl & FM);
            d32 = (u32) a; asm volatile("btl $0, %k[c]; sbbl %[s], %[d]; pushfq; popq %[f]" : [d]"+r"(d32), [f]"=r"(fl) : [s]"r"(s32), [c]"r"((u32) cf) : "cc"); mix(d32); mix(fl & FM);
            d = a; asm volatile("btl $0, %k[c]; adcq %[m], %[d]; pushfq; popq %[f]" : [d]"+r"(d), [f]"=r"(fl) : [m]"m"(m), [c]"r"((u32) cf) : "cc"); mix(d); mix(fl & FM);
            m = a; asm volatile("btl $0, %k[c]; sbbq %[s], %[m]; pushfq; popq %[f]" : [m]"+m"(m), [f]"=r"(fl) : [s]"r"(s), [c]"r"((u32) cf) : "cc"); mix(m); mix(fl & FM);
            d = a; asm volatile("btl $0, %k[c]; adcq $0x7f, %[d]; pushfq; popq %[f]" : [d]"+r"(d), [f]"=r"(fl) : [c]"r"((u32) cf) : "cc"); mix(d); mix(fl & FM);
            d = a; asm volatile("btl $0, %k[c]; sbbq $-2, %[d]; pushfq; popq %[f]" : [d]"+r"(d), [f]"=r"(fl) : [c]"r"((u32) cf) : "cc"); mix(d); mix(fl & FM);
            d32 = (u32) a; asm volatile("btl $0, %k[c]; adcl $0x12345, %[d]; pushfq; popq %[f]" : [d]"+r"(d32), [f]"=r"(fl) : [c]"r"((u32) cf) : "cc"); mix(d32); mix(fl & FM);
            d32 = (u32) a; asm volatile("btl $0, %k[c]; sbbl $0x12345, %[d]; pushfq; popq %[f]" : [d]"+r"(d32), [f]"=r"(fl) : [c]"r"((u32) cf) : "cc"); mix(d32); mix(fl & FM);
            d = a; asm volatile("btl $0, %k[c]; adcq %[s], %[d]; cmpl %k[f], %k[f]" : [d]"+r"(d), [f]"=r"(fl) : [s]"r"(s), [c]"r"((u32) cf) : "cc"); mix(d);   /* flags dead */
            d = a; asm volatile("btl $0, %k[c]; sbbq %[s], %[d]; cmpl %k[f], %k[f]" : [d]"+r"(d), [f]"=r"(fl) : [s]"r"(s), [c]"r"((u32) cf) : "cc"); mix(d);
        }
        {   /* the carry comes from the add/sub/logic before; a conditional jump after reads the lazily kept flags */
            u64 d = a, s = b, hi = a ^ b, lo = b; u64 r1, r2, o;
            asm volatile("addq %[s], %[d]; adcq %[h], %[l]; setc %b[o]; adcq $0, %[h]" : [d]"+r"(d), [l]"+r"(lo), [h]"+r"(hi), [o]"=q"(o) : [s]"r"(s) : "cc"); mix(d); mix(lo); mix(hi); mix(o & 1);
            r1 = a; r2 = b; asm volatile("subq %[s], %[d]; sbbq %[s], %[d]; sbbq $0, %[d]; setb %b[o]" : [d]"+r"(r1), [o]"=q"(o) : [s]"r"(r2) : "cc"); mix(r1); mix(o & 1);
            r1 = a; asm volatile("andq %[s], %[d]; adcq %[s], %[d]; sets %b[o]" : [d]"+r"(r1), [o]"=q"(o) : [s]"r"(b) : "cc"); mix(r1); mix(o & 1);
            r1 = a; asm volatile("stc; adcq %[s], %[d]; jo 1f; movq $1, %[o]; jmp 2f; 1: movq $2, %[o]; 2:" : [d]"+r"(r1), [o]"=r"(o) : [s]"r"(b) : "cc"); mix(r1); mix(o);
        }
        /* shifts by cl, flags live: the old flags (set up with stc/clc and a compare) must survive a zero count */
        {
            static const int cnt[] = { 0, 1, 2, 7, 31, 32, 33, 63, 64, 65, 0x100, 0x101 };
            for (int k = 0; k < 12; k++) {
                u64 d, fl, c = (u64) cnt[k]; u32 d32; u64 fm64 = (cnt[k] & 63) == 0 ? FM : 0xc5ULL, fm32 = (cnt[k] & 31) == 0 ? FM : 0xc5ULL;
                d = a; asm volatile("cmpq %[q], %[p]; stc; shlq %%cl, %[d]; pushfq; popq %[f]" : [d]"+r"(d), [f]"=r"(fl) : "c"(c), [p]"r"(a), [q]"r"(b) : "cc"); mix(d); mix(fl & fm64);
                d = a; asm volatile("cmpq %[q], %[p]; clc; shrq %%cl, %[d]; pushfq; popq %[f]" : [d]"+r"(d), [f]"=r"(fl) : "c"(c), [p]"r"(a), [q]"r"(b) : "cc"); mix(d); mix(fl & fm64);
                d = a; asm volatile("cmpq %[q], %[p]; stc; sarq %%cl, %[d]; pushfq; popq %[f]" : [d]"+r"(d), [f]"=r"(fl) : "c"(c), [p]"r"(a), [q]"r"(b) : "cc"); mix(d); mix(fl & fm64);
                d32 = (u32) a; asm volatile("cmpq %[q], %[p]; clc; shll %%cl, %[d]; pushfq; popq %[f]" : [d]"+r"(d32), [f]"=r"(fl) : "c"(c), [p]"r"(a), [q]"r"(b) : "cc"); mix(d32); mix(fl & fm32);
                d32 = (u32) a; asm volatile("cmpq %[q], %[p]; stc; shrl %%cl, %[d]; pushfq; popq %[f]" : [d]"+r"(d32), [f]"=r"(fl) : "c"(c), [p]"r"(a), [q]"r"(b) : "cc"); mix(d32); mix(fl & fm32);
                d32 = (u32) a; asm volatile("cmpq %[q], %[p]; clc; sarl %%cl, %[d]; pushfq; popq %[f]" : [d]"+r"(d32), [f]"=r"(fl) : "c"(c), [p]"r"(a), [q]"r"(b) : "cc"); mix(d32); mix(fl & fm32);
                d = a; asm volatile("cmpq %[b], %[d]; shrq %%cl, %[d]; pushfq; popq %[f]" : [d]"+r"(d), [f]"=r"(fl) : "c"(c), [b]"r"(b) : "cc"); mix(d); mix(fl & fm64);
                *(u64*) buf = a; asm volatile("cmpq %[q], %[p]; stc; shrq %%cl, %[m]; pushfq; popq %[f]" : [m]"+m"(*(u64*) buf), [f]"=r"(fl) : "c"(c), [p]"r"(a), [q]"r"(b) : "cc", "memory"); mix(*(u64*) buf); mix(fl & fm64);
                *(u32*)(buf + 8) = (u32) a; asm volatile("cmpq %[q], %[p]; clc; sarl %%cl, %[m]; pushfq; popq %[f]" : [m]"+m"(*(u32*)(buf + 8)), [f]"=r"(fl) : "c"(c), [p]"r"(a), [q]"r"(b) : "cc", "memory"); mix(*(u32*)(buf + 8)); mix(fl & fm32);
                /* jcc / setcc straight after: the condition helper reads the lazy shift state */
                d = a; fl = 0; asm volatile("cmpq %[q], %[p]; shlq %%cl, %[d]; setz %b[o]" : [d]"+r"(d), [o]"+q"(fl) : "c"(c), [p]"r"(a), [q]"r"(b) : "cc"); mix(d); mix(fl & 1);
                d = a; asm volatile("cmpq %[q], %[p]; stc; shrq %%cl, %[d]; jc 1f; movq $1, %[o]; jmp 2f; 1: movq $2, %[o]; 2:" : [d]"+r"(d), [o]"=r"(fl) : "c"(c), [p]"r"(a), [q]"r"(b) : "cc"); mix(d); mix(fl);
                d = a; asm volatile("cmpq %[q], %[p]; shrq %%cl, %[d]; cmpl %k[f], %k[f]" : [d]"+r"(d), [f]"=r"(fl) : "c"(c), [p]"r"(a), [q]"r"(b) : "cc"); mix(d);    /* flags dead */
            }
        }
        /* push m64, xchg rax,r */
        {
            u64 v = a, r1, r2, x = a, y = b, src2[2] = { a, b }; u32 x32 = (u32) a, y32 = (u32) b; u16 x16 = (u16) a, y16 = (u16) b;
            asm volatile("subq $128, %%rsp; pushq (%[p]); popq %[r]; addq $128, %%rsp" : [r]"=&r"(r1) : [p]"r"(&v) : "cc", "memory"); mix(r1);
            asm volatile("pushq 8(%[p]); popq %[r]" : [r]"=&r"(r1) : [p]"r"(&src2[0]) : "cc", "memory"); mix(r1);          /* below the red zone is not touched: push/pop pair */
            asm volatile("subq $128, %%rsp; pushq %[a]; pushq (%%rsp); popq %[r]; popq %[q]; addq $128, %%rsp" : [r]"=r"(r1), [q]"=r"(r2) : [a]"r"(v) : "cc", "memory"); mix(r1); mix(r2);
            asm volatile("xchgq %[b], %%rax" : "+a"(x), [b]"+r"(y)); mix(x); mix(y);
            asm volatile("xchgl %[b], %%eax" : "+a"(x32), [b]"+r"(y32)); mix(x32); mix(y32);
            asm volatile("xchgw %[b], %%ax" : "+a"(x16), [b]"+r"(y16)); mix(x16); mix(y16);
            register u64 r8v asm("r8") = a; x = b; asm volatile("xchgq %%r8, %%rax" : "+a"(x), "+r"(r8v)); mix(x); mix(r8v);
        }
        /* movq xmm, xmm/m64 (F3 0F 7E): the upper half becomes zero */
        {
            u64 src[2] = { a, b }, out[2]; 
            asm volatile("pcmpeqd %%xmm0, %%xmm0; movq (%[p]), %%xmm0; movdqu %%xmm0, (%[o])" : : [p]"r"(src), [o]"r"(out) : "xmm0", "memory"); mix(out[0]); mix(out[1]);
            asm volatile("pcmpeqd %%xmm0, %%xmm0; movdqu (%[p]), %%xmm1; movq %%xmm1, %%xmm0; movdqu %%xmm0, (%[o])" : : [p]"r"(src), [o]"r"(out) : "xmm0", "xmm1", "memory"); mix(out[0]); mix(out[1]);
        }
        /* rol / ror r/m8|16, imm8 with the flags dead */
        {
            u16 w = (u16) a, w2 = (u16) b; u8 q = (u8) a, q2 = (u8) b; u64 z;
            asm volatile("rolw $8, %[w]; cmpl %k[z], %k[z]" : [w]"+r"(w), [z]"=r"(z) : : "cc"); mix(w);
            asm volatile("rolw $5, %[w]; cmpl %k[z], %k[z]" : [w]"+r"(w2), [z]"=r"(z) : : "cc"); mix(w2);
            asm volatile("rorw $3, %[w]; cmpl %k[z], %k[z]" : [w]"+r"(w), [z]"=r"(z) : : "cc"); mix(w);
            asm volatile("rolb $3, %[q]; cmpl %k[z], %k[z]" : [q]"+q"(q), [z]"=r"(z) : : "cc"); mix(q);
            asm volatile("rorb $1, %[q]; cmpl %k[z], %k[z]" : [q]"+q"(q2), [z]"=r"(z) : : "cc"); mix(q2);
            *(u16*)(buf + 20) = (u16) a; asm volatile("rolw $8, %[m]; cmpl %k[z], %k[z]" : [m]"+m"(*(u16*)(buf + 20)), [z]"=r"(z) : : "cc", "memory"); mix(*(u16*)(buf + 20));
            w = (u16) a; asm volatile("rolw $8, %[w]; pushfq; popq %[z]" : [w]"+r"(w), [z]"=r"(z) : : "cc"); mix(w); mix(z & 1);                    /* flags live: interpreter */
        }
    }
    printf("%016llx\n", (unsigned long long) sum);
    return 0;
}
