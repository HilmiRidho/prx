/* comiss, comisd, ucomiss and ucomisd followed by every condition (setcc, jcc, cmovcc), and div/idiv: normal, wide dividend, zero divisor, overflow.
 * The expected output is what a real x86-64 prints. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <signal.h>
#include <setjmp.h>

#define SETALL "seto 0(%[o])\n setno 1(%[o])\n setb 2(%[o])\n setae 3(%[o])\n sete 4(%[o])\n setne 5(%[o])\n setbe 6(%[o])\n seta 7(%[o])\n" \
               "sets 8(%[o])\n setns 9(%[o])\n setp 10(%[o])\n setnp 11(%[o])\n setl 12(%[o])\n setge 13(%[o])\n setle 14(%[o])\n setg 15(%[o])\n"
static unsigned mask16(unsigned char *o) { unsigned m = 0; for (int i = 0; i < 16; i++) m |= (unsigned) (o[i] & 1) << i; return m; }
static unsigned t_comisd(double a, double b)  { unsigned char o[16]; __asm__ volatile("comisd %[b], %[a]\n" SETALL :: [a]"x"(a), [b]"x"(b), [o]"r"(o) : "memory", "cc"); return mask16(o); }
static unsigned t_ucomisd(double a, double b) { unsigned char o[16]; __asm__ volatile("ucomisd %[b], %[a]\n" SETALL :: [a]"x"(a), [b]"x"(b), [o]"r"(o) : "memory", "cc"); return mask16(o); }
static unsigned t_comiss(float a, float b)    { unsigned char o[16]; __asm__ volatile("comiss %[b], %[a]\n" SETALL :: [a]"x"(a), [b]"x"(b), [o]"r"(o) : "memory", "cc"); return mask16(o); }
static unsigned t_ucomiss(float a, float b)   { unsigned char o[16]; __asm__ volatile("ucomiss %[b], %[a]\n" SETALL :: [a]"x"(a), [b]"x"(b), [o]"r"(o) : "memory", "cc"); return mask16(o); }
static unsigned t_mem(double a, double *b)    { unsigned char o[16]; __asm__ volatile("comisd (%[b]), %[a]\n" SETALL :: [a]"x"(a), [b]"r"(b), [o]"r"(o) : "memory", "cc"); return mask16(o); }
static unsigned t_jcc(double a, double b) {                       /* jb/jae/jbe/ja/jp/jnp/je/jne as real branches */
    unsigned r = 0, k;
#define J(cc, bit) k = 0; __asm__ volatile("comisd %[b], %[a]\n j" cc " 1f\n jmp 2f\n 1: movl $1, %[k]\n 2:\n" : [k]"+r"(k) : [a]"x"(a), [b]"x"(b) : "cc"); r |= k << bit;
    J("b", 0) J("ae", 1) J("be", 2) J("a", 3) J("p", 4) J("np", 5) J("e", 6) J("ne", 7)
#undef J
    return r;
}
static unsigned t_cmov(double a, double b) {
    unsigned x = 0, y = 0, z = 0;
    __asm__ volatile("comisd %[b], %[a]\n movl $7, %%eax\n cmovb %[c], %[x]\n cmovnp %[c], %[y]\n cmova %[c], %[z]\n"
                     : [x]"+r"(x), [y]"+r"(y), [z]"+r"(z) : [a]"x"(a), [b]"x"(b), [c]"r"(5u) : "cc", "eax");
    return x * 100 + y * 10 + z;
}

static sigjmp_buf jb; static volatile int got;
static void on_fpe(int s) { (void) s; got = 1; siglongjmp(jb, 1); }
#define DIVT(name, ty, ins) \
static void name(ty rdx_in, ty rax_in, ty dv) { ty q = 0, r = 0; got = 0; \
    if (!sigsetjmp(jb, 1)) { ty a = rax_in, h = rdx_in; __asm__ volatile(ins " %[dd]" : "+a"(a), "+d"(h) : [dd]"r"(dv) : "cc"); q = a; r = h; } \
    if (got) printf(#name " %lld:%lld / %lld -> SIGFPE\n", (long long) rdx_in, (long long) rax_in, (long long) dv); \
    else printf(#name " %lld:%lld / %lld -> %lld r %lld\n", (long long) rdx_in, (long long) rax_in, (long long) dv, (long long) q, (long long) r); }
DIVT(udiv64, uint64_t, "divq")
DIVT(idiv64, int64_t, "idivq")
DIVT(udiv32, uint32_t, "divl")
DIVT(idiv32, int32_t, "idivl")

int main(void) {
    double v[] = { 0.0, -0.0, 1.0, -1.0, 2.5, 1e300, -1e300, INFINITY, -INFINITY, NAN, 1.0000000001 };
    int n = sizeof v / sizeof v[0]; unsigned h = 0;
    for (int i = 0; i < n; i++) for (int k = 0; k < n; k++) {
        double a = v[i], b = v[k]; float fa = (float) a, fb = (float) b;
        unsigned m[7] = { t_comisd(a, b), t_ucomisd(a, b), t_comiss(fa, fb), t_ucomiss(fa, fb), t_mem(a, &b), t_jcc(a, b), t_cmov(a, b) };
        for (int j = 0; j < 7; j++) h = h * 31 + m[j];
        if (i < 4 || k == 9) printf("%d %d: %#x %#x %#x %#x %#x %#x %u\n", i, k, m[0], m[1], m[2], m[3], m[4], m[5], m[6]);
    }
    printf("hash %u\n", h);
    signal(SIGFPE, on_fpe);
    udiv64(0, 100, 7); udiv64(0, 100, 0); udiv64(1, 0, 3); udiv64(0, ~0ULL, 1); udiv64(5, 0, 3); udiv64(3, 5, 3);
    idiv64(0, -100, 7); idiv64(-1, -100, 7); idiv64(0, 100, -7); idiv64(0, INT64_MIN, -1); idiv64(-1, INT64_MIN, -1); idiv64(0, 1, 0); idiv64(1, 0, 5); idiv64(0, 100, -1);
    udiv32(0, 100, 7); udiv32(0, 100, 0); udiv32(1, 0, 3); udiv32(0, 0xffffffffu, 0xffffffffu); udiv32(7, 0, 7); udiv32(0, 12345, 1);
    idiv32(0, -100, 7); idiv32(-1, -100, 7); idiv32(0, 100, -7); idiv32(0, INT32_MIN, -1); idiv32(-1, INT32_MIN, -1); idiv32(0, 1, 0); idiv32(1, 0, 5);
    uint64_t s = 0; for (uint64_t c = 2; c < 3000; c++) { uint64_t nn = 1000003; s += nn % c; if ((double) c > sqrt((double) nn)) break; }
    printf("loop %llu\n", (unsigned long long) s);
    return 0;
}
