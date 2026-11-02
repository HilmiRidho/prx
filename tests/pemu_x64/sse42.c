/* SSE3 / SSSE3 / SSE4.1 / SSE4.2, cmpxchg8b/16b, movnti, xgetbv, rdtscp: results are hashed per instruction, so the
 * expected output was produced by the same program on a real x86-64 CPU (see sse42.expect). Build: -msse4.2 -mcx16 -pthread. */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/mman.h>
#include <cpuid.h>
#include <immintrin.h>

typedef unsigned long long u64;
static u64 H = 0xcbf29ce484222325ULL;
static void mix(const void*p, int n) { const unsigned char*b = p; for (int i = 0; i < n; i++) { H ^= b[i]; H *= 0x100000001b3ULL; } }
static void mixi(long long v) { mix(&v, 8); }
static void report(const char*name) { printf("%-16s %016llx\n", name, H); H = 0xcbf29ce484222325ULL; }
static u64 rs = 88172645463325252ULL;
static u64 rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
#define MIXV(v) do { __m128i t_ = (__m128i)(v); mix(&t_, 16); } while (0)

static __m128i ri(void) {                       /* bytes biased towards the values that hit saturation and sign cases */
    unsigned char b[16];
    for (int i = 0; i < 16; i++) { u64 r = rnd(); int k = (int)(r % 8); b[i] = k == 0 ? 0 : k == 1 ? 0x7f : k == 2 ? 0x80 : k == 3 ? 0xff : k == 4 ? 1 : (unsigned char)(r >> 8); }
    return _mm_loadu_si128((const __m128i*) b);
}
static __m128 rf(void) { float f[4]; for (int i = 0; i < 4; i++) f[i] = (float)((int)(rnd() % 4001) - 2000) / 4.0f; return _mm_loadu_ps(f); }   /* finite, quarters: ties and exact sums */
static __m128d rd(void) { double f[2]; for (int i = 0; i < 2; i++) f[i] = (double)((int)(rnd() % 4001) - 2000) / 4.0; return _mm_loadu_pd(f); }
static __m128i rstr(int word) {                 /* short alphabet with embedded zeros: string-compare instructions need matches and terminators */
    static const unsigned char bp[9] = {0, 0, 'a', 'b', 'c', 'd', 0x80, 0xff, 'a'};
    static const unsigned short wp[9] = {0, 0, 'a', 'b', 'c', 'd', 0x8000, 0xff80, 0x0100};
    unsigned char b[16];
    for (int i = 0; i < (word ? 8 : 16); i++) { int k = (int)(rnd() % 9); if (word) ((unsigned short*) b)[i] = wp[k]; else b[i] = bp[k]; }
    return _mm_loadu_si128((const __m128i*) b);
}
#define N 1500
#define T2(NAME, EXPR) do { for (int i_ = 0; i_ < N; i_++) { __m128i a = ri(), b = ri(); (void) a; (void) b; MIXV(EXPR); } report(NAME); } while (0)
#define T1(NAME, EXPR) do { for (int i_ = 0; i_ < N; i_++) { __m128i a = ri(); (void) a; MIXV(EXPR); } report(NAME); } while (0)
#define TF2(NAME, EXPR) do { for (int i_ = 0; i_ < N; i_++) { __m128 a = rf(), b = rf(); (void) a; (void) b; MIXV(EXPR); } report(NAME); } while (0)
#define TD2(NAME, EXPR) do { for (int i_ = 0; i_ < N; i_++) { __m128d a = rd(), b = rd(); (void) a; (void) b; MIXV(EXPR); } report(NAME); } while (0)

#define IMM_LIST(X) X(0x00) X(0x01) X(0x02) X(0x03) X(0x04) X(0x05) X(0x06) X(0x07) X(0x08) X(0x09) X(0x0a) X(0x0b) X(0x0c) X(0x0d) X(0x0e) X(0x0f) X(0x10) X(0x11) X(0x12) X(0x13) X(0x14) X(0x15) X(0x16) X(0x17) X(0x18) X(0x19) X(0x1a) X(0x1b) X(0x1c) X(0x1d) X(0x1e) X(0x1f) X(0x20) X(0x21) X(0x22) X(0x23) X(0x24) X(0x25) X(0x26) X(0x27) X(0x28) X(0x29) X(0x2a) X(0x2b) X(0x2c) X(0x2d) X(0x2e) X(0x2f) X(0x30) X(0x31) X(0x32) X(0x33) X(0x34) X(0x35) X(0x36) X(0x37) X(0x38) X(0x39) X(0x3a) X(0x3b) X(0x3c) X(0x3d) X(0x3e) X(0x3f) X(0x40) X(0x41) X(0x42) X(0x43) X(0x44) X(0x45) X(0x46) X(0x47) X(0x48) X(0x49) X(0x4a) X(0x4b) X(0x4c) X(0x4d) X(0x4e) X(0x4f) X(0x50) X(0x51) X(0x52) X(0x53) X(0x54) X(0x55) X(0x56) X(0x57) X(0x58) X(0x59) X(0x5a) X(0x5b) X(0x5c) X(0x5d) X(0x5e) X(0x5f) X(0x60) X(0x61) X(0x62) X(0x63) X(0x64) X(0x65) X(0x66) X(0x67) X(0x68) X(0x69) X(0x6a) X(0x6b) X(0x6c) X(0x6d) X(0x6e) X(0x6f) X(0x70) X(0x71) X(0x72) X(0x73) X(0x74) X(0x75) X(0x76) X(0x77) X(0x78) X(0x79) X(0x7a) X(0x7b) X(0x7c) X(0x7d) X(0x7e) X(0x7f)
#define PCMP(IMM) do { for (int i_ = 0; i_ < 12; i_++) { \
    __m128i a = rstr((IMM) & 1), b = rstr((IMM) & 1); int la = (int)(rnd() % 22) - 3, lb = (int)(rnd() % 22) - 3; \
    mixi(_mm_cmpistri(a, b, IMM)); mixi(_mm_cmpistrc(a, b, IMM)); mixi(_mm_cmpistrz(a, b, IMM)); mixi(_mm_cmpistrs(a, b, IMM)); mixi(_mm_cmpistro(a, b, IMM)); mixi(_mm_cmpistra(a, b, IMM)); \
    MIXV(_mm_cmpistrm(a, b, IMM)); \
    mixi(_mm_cmpestri(a, la, b, lb, IMM)); mixi(_mm_cmpestrc(a, la, b, lb, IMM)); mixi(_mm_cmpestrz(a, la, b, lb, IMM)); mixi(_mm_cmpestrs(a, la, b, lb, IMM)); \
    mixi(_mm_cmpestro(a, la, b, lb, IMM)); mixi(_mm_cmpestra(a, la, b, lb, IMM)); MIXV(_mm_cmpestrm(a, la, b, lb, IMM)); } } while (0)

/* ---- cmpxchg16b / cmpxchg8b */
typedef struct { u64 lo, hi; } __attribute__((aligned(16))) P16;
static int cas16x(P16*p, P16*e, u64 n0, u64 n1) {
    unsigned char ok; __asm__ __volatile__("lock cmpxchg16b %[m]\n\tsetz %[ok]" : [m] "+m"(*p), "+a"(e->lo), "+d"(e->hi), [ok] "=q"(ok) : "b"(n0), "c"(n1) : "cc", "memory");
    return ok;
}
static int cas8x(volatile u64*p, u64*e, u64 n) {
    unsigned char ok; unsigned el = (unsigned) *e, eh = (unsigned)(*e >> 32);
    __asm__ __volatile__("lock cmpxchg8b %[m]\n\tsetz %[ok]" : [m] "+m"(*p), "+a"(el), "+d"(eh), [ok] "=q"(ok) : "b"((unsigned) n), "c"((unsigned)(n >> 32)) : "cc", "memory");
    *e = ((u64) eh << 32) | el; return ok;
}
static P16 ctr;
static void*worker(void*arg) {
    (void) arg; P16 e = {0, 0};
    cas16x(&ctr, &e, 0, 0);                                    /* the first compare is allowed to fail: it loads the current value */
    for (int i = 0; i < 20000; i++) while (!cas16x(&ctr, &e, e.lo + 1, e.hi + 3)) { }
    return NULL;
}

/* ---- the glibc string functions pick their SSE4.2 variants now: check them against plain loops */
static int bad;
static void chk(const char*what, long got, long want, const char*a, const char*b) {
    if (got != want) { if (bad++ < 8) printf("MISMATCH %s: got %ld want %ld (\"%s\", \"%s\")\n", what, got, want, a, b); }
}
static void strings(void) {
    char a[48], b[24];
    static const char al[] = "abcdAB \tx";
    size_t (*volatile p_cspn)(const char*, const char*) = strcspn, (*volatile p_spn)(const char*, const char*) = strspn;
    char*(*volatile p_pbrk)(const char*, const char*) = strpbrk, *(*volatile p_str)(const char*, const char*) = strstr;
    int (*volatile p_cmp)(const char*, const char*) = strcasecmp;
    for (int it = 0; it < 4000; it++) {
        int la = (int)(rnd() % 40), lb = (int)(rnd() % 6) + (it & 1);
        for (int i = 0; i < la; i++) a[i] = al[rnd() % (sizeof al - 1)];
        a[la] = 0;
        for (int i = 0; i < lb; i++) b[i] = al[rnd() % (sizeof al - 1)];
        b[lb] = 0;
        size_t cs = 0, sp = 0; const char*pb = NULL, *ps = NULL;
        while (a[cs] && !strchr(b, a[cs])) cs++;
        while (a[sp] && strchr(b, a[sp]) && b[0]) sp++;
        for (const char*q = a; *q; q++) if (strchr(b, *q)) { pb = q; break; }
        if (!b[0]) cs = (size_t) la;
        for (int i = 0; i + lb <= la && !ps; i++) if (!memcmp(a + i, b, (size_t) lb)) ps = a + i;
        int cc = 0; { int i = 0; for (; a[i] && b[i]; i++) { int x = a[i] | 0x20, y = b[i] | 0x20; if (a[i] < 'A' || (a[i] > 'Z' && a[i] < 'a') || a[i] > 'z') x = a[i]; if (b[i] < 'A' || (b[i] > 'Z' && b[i] < 'a') || b[i] > 'z') y = b[i]; if (x != y) { cc = x < y ? -1 : 1; break; } }
                      if (!cc) cc = a[i] == b[i] ? 0 : !a[i] ? -1 : !b[i] ? 1 : 0; }
        chk("strcspn", (long) p_cspn(a, b), (long) cs, a, b);
        if (b[0]) chk("strspn", (long) p_spn(a, b), (long) sp, a, b);
        chk("strpbrk", p_pbrk(a, b) ? (long)(p_pbrk(a, b) - a) : -1, pb ? (long)(pb - a) : -1, a, b);
        chk("strstr", p_str(a, b) ? (long)(p_str(a, b) - a) : -1, ps ? (long)(ps - a) : -1, a, b);
        { int g = p_cmp(a, b); chk("strcasecmp", (g > 0) - (g < 0), cc, a, b); }
        { int g = memcmp(a, b, (size_t)(la < lb ? la : lb)); (void) g; }
    }
    printf("strings: %s\n", bad ? "FAIL" : "ok");
}

int main(void) {
    unsigned a_, b_, c_, d_; __cpuid(1, a_, b_, c_, d_);
    printf("cpuid: sse3=%u ssse3=%u cx16=%u sse41=%u sse42=%u popcnt=%u\n", c_ & 1, (c_ >> 9) & 1, (c_ >> 13) & 1, (c_ >> 19) & 1, (c_ >> 20) & 1, (c_ >> 23) & 1);

    /* SSE3 */
    TF2("haddps", _mm_hadd_ps(a, b)); TF2("hsubps", _mm_hsub_ps(a, b)); TF2("addsubps", _mm_addsub_ps(a, b));
    TD2("haddpd", _mm_hadd_pd(a, b)); TD2("hsubpd", _mm_hsub_pd(a, b)); TD2("addsubpd", _mm_addsub_pd(a, b));
    TF2("movshdup", _mm_movehdup_ps(a)); TF2("movsldup", _mm_moveldup_ps(a)); TD2("movddup", _mm_movedup_pd(a));
    { double v[2] = {1.5, -2.25}; for (int i = 0; i < N; i++) { v[0] += 1; __m128d r = _mm_loaddup_pd(v); MIXV(r); } report("movddup-mem"); }
    { char buf[64]; for (int i = 0; i < 64; i++) buf[i] = (char) rnd(); for (int i = 0; i < 40; i++) { __m128i r = _mm_lddqu_si128((const __m128i*)(buf + i)); MIXV(r); } report("lddqu"); }
    /* SSSE3 */
    T2("pshufb", _mm_shuffle_epi8(a, b)); T2("phaddw", _mm_hadd_epi16(a, b)); T2("phaddd", _mm_hadd_epi32(a, b)); T2("phaddsw", _mm_hadds_epi16(a, b));
    T2("phsubw", _mm_hsub_epi16(a, b)); T2("phsubd", _mm_hsub_epi32(a, b)); T2("phsubsw", _mm_hsubs_epi16(a, b)); T2("pmaddubsw", _mm_maddubs_epi16(a, b));
    T2("psignb", _mm_sign_epi8(a, b)); T2("psignw", _mm_sign_epi16(a, b)); T2("psignd", _mm_sign_epi32(a, b)); T2("pmulhrsw", _mm_mulhrs_epi16(a, b));
    T1("pabsb", _mm_abs_epi8(a)); T1("pabsw", _mm_abs_epi16(a)); T1("pabsd", _mm_abs_epi32(a));
    T2("palignr0", _mm_alignr_epi8(a, b, 0)); T2("palignr3", _mm_alignr_epi8(a, b, 3)); T2("palignr8", _mm_alignr_epi8(a, b, 8)); T2("palignr15", _mm_alignr_epi8(a, b, 15));
    T2("palignr16", _mm_alignr_epi8(a, b, 16)); T2("palignr20", _mm_alignr_epi8(a, b, 20)); T2("palignr31", _mm_alignr_epi8(a, b, 31)); T2("palignr40", _mm_alignr_epi8(a, b, 40));
    /* SSE4.1 */
    for (int i = 0; i < N; i++) { __m128i a = ri(), b = ri(), m = ri(); __m128i r = _mm_blendv_epi8(a, b, m); MIXV(r); } report("pblendvb");
    for (int i = 0; i < N; i++) { __m128 a = rf(), b = rf(); __m128 m = _mm_castsi128_ps(ri()); __m128 r = _mm_blendv_ps(a, b, m); MIXV(r); } report("blendvps");
    for (int i = 0; i < N; i++) { __m128d a = rd(), b = rd(); __m128d m = _mm_castsi128_pd(ri()); __m128d r = _mm_blendv_pd(a, b, m); MIXV(r); } report("blendvpd");
    for (int i = 0; i < N; i++) { __m128i a = ri(), b = (i & 3) == 0 ? _mm_and_si128(ri(), a) : ri(); if (i & 4) b = _mm_andnot_si128(a, b);
                                  mixi(_mm_testz_si128(a, b)); mixi(_mm_testc_si128(a, b)); mixi(_mm_testnzc_si128(a, b)); } report("ptest");
    T1("pmovsxbw", _mm_cvtepi8_epi16(a)); T1("pmovsxbd", _mm_cvtepi8_epi32(a)); T1("pmovsxbq", _mm_cvtepi8_epi64(a)); T1("pmovsxwd", _mm_cvtepi16_epi32(a));
    T1("pmovsxwq", _mm_cvtepi16_epi64(a)); T1("pmovsxdq", _mm_cvtepi32_epi64(a)); T1("pmovzxbw", _mm_cvtepu8_epi16(a)); T1("pmovzxbd", _mm_cvtepu8_epi32(a));
    T1("pmovzxbq", _mm_cvtepu8_epi64(a)); T1("pmovzxwd", _mm_cvtepu16_epi32(a)); T1("pmovzxwq", _mm_cvtepu16_epi64(a)); T1("pmovzxdq", _mm_cvtepu32_epi64(a));
    T2("pmuldq", _mm_mul_epi32(a, b)); T2("pcmpeqq", _mm_cmpeq_epi64(a, b)); T2("pcmpgtq", _mm_cmpgt_epi64(a, b)); T2("packusdw", _mm_packus_epi32(a, b));
    T2("pminsb", _mm_min_epi8(a, b)); T2("pminsd", _mm_min_epi32(a, b)); T2("pminuw", _mm_min_epu16(a, b)); T2("pminud", _mm_min_epu32(a, b));
    T2("pmaxsb", _mm_max_epi8(a, b)); T2("pmaxsd", _mm_max_epi32(a, b)); T2("pmaxuw", _mm_max_epu16(a, b)); T2("pmaxud", _mm_max_epu32(a, b));
    T2("pmulld", _mm_mullo_epi32(a, b)); T1("phminposuw", _mm_minpos_epu16(a));
    TF2("roundps-n", _mm_round_ps(a, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC)); TF2("roundps-f", _mm_round_ps(a, _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC));
    TF2("roundps-c", _mm_round_ps(a, _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC)); TF2("roundps-t", _mm_round_ps(a, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC));
    TF2("roundps-cur", _mm_round_ps(a, _MM_FROUND_CUR_DIRECTION)); TF2("roundss-f", _mm_round_ss(a, b, _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC));
    TF2("roundss-t", _mm_round_ss(a, b, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC));
    TD2("roundpd-n", _mm_round_pd(a, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC)); TD2("roundpd-c", _mm_round_pd(a, _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC));
    TD2("roundsd-f", _mm_round_sd(a, b, _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC)); TD2("roundsd-cur", _mm_round_sd(a, b, _MM_FROUND_CUR_DIRECTION));
    TF2("blendps5", _mm_blend_ps(a, b, 5)); TF2("blendps10", _mm_blend_ps(a, b, 10)); TD2("blendpd1", _mm_blend_pd(a, b, 1)); TD2("blendpd2", _mm_blend_pd(a, b, 2));
    T2("pblendw", _mm_blend_epi16(a, b, 0xA5)); T2("pblendw2", _mm_blend_epi16(a, b, 0x3C));
    for (int i = 0; i < N; i++) { __m128i a = ri(); mixi(_mm_extract_epi8(a, 0)); mixi(_mm_extract_epi8(a, 7)); mixi(_mm_extract_epi8(a, 15)); mixi(_mm_extract_epi16(a, 3));
        mixi(_mm_extract_epi32(a, 0)); mixi(_mm_extract_epi32(a, 3)); mixi(_mm_extract_epi64(a, 0)); mixi(_mm_extract_epi64(a, 1)); } report("pextr");
    for (int i = 0; i < N; i++) { __m128 a = rf(); mixi(_mm_extract_ps(a, 0)); mixi(_mm_extract_ps(a, 2)); } report("extractps");
    for (int i = 0; i < N; i++) { __m128i a = ri(); int v = (int) rnd(); long long q = (long long) rnd(); __m128i r;
        r = _mm_insert_epi8(a, v, 0); MIXV(r); r = _mm_insert_epi8(a, v, 11); MIXV(r); r = _mm_insert_epi32(a, v, 1); MIXV(r); r = _mm_insert_epi32(a, v, 3); MIXV(r);
        r = _mm_insert_epi64(a, q, 0); MIXV(r); r = _mm_insert_epi64(a, q, 1); MIXV(r); } report("pinsr");
    TF2("insertps0", _mm_insert_ps(a, b, 0x00)); TF2("insertps1", _mm_insert_ps(a, b, 0x9D)); TF2("insertps2", _mm_insert_ps(a, b, 0x6A)); TF2("insertps3", _mm_insert_ps(a, b, 0xF1));
    TF2("dpps-ff", _mm_dp_ps(a, b, 0xFF)); TF2("dpps-71", _mm_dp_ps(a, b, 0x71)); TF2("dpps-e5", _mm_dp_ps(a, b, 0xE5)); TD2("dppd-31", _mm_dp_pd(a, b, 0x31)); TD2("dppd-ff", _mm_dp_pd(a, b, 0xFF));
    T2("mpsadbw0", _mm_mpsadbw_epu8(a, b, 0)); T2("mpsadbw1", _mm_mpsadbw_epu8(a, b, 1)); T2("mpsadbw2", _mm_mpsadbw_epu8(a, b, 2)); T2("mpsadbw3", _mm_mpsadbw_epu8(a, b, 3));
    T2("mpsadbw4", _mm_mpsadbw_epu8(a, b, 4)); T2("mpsadbw5", _mm_mpsadbw_epu8(a, b, 5)); T2("mpsadbw6", _mm_mpsadbw_epu8(a, b, 6)); T2("mpsadbw7", _mm_mpsadbw_epu8(a, b, 7));
    /* memory operands that stop at the end of a page: only the bytes the instruction reads may be touched */
    {
        unsigned char*pg = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        mprotect(pg + 4096, 4096, PROT_NONE);
        for (int i = 0; i < 16; i++) pg[4096 - 16 + i] = (unsigned char)(0x80 + i * 7);
        __m128i r = _mm_set1_epi8(0x11); __m128 f = _mm_set1_ps(5.0f); __m128d g = _mm_set1_pd(7.0);
#define MEMOP(NAME, INSN, BYTES) do { __m128i o_; __asm__("movdqa %1, %0\n\t" INSN : "=x"(o_) : "x"(r), "r"(pg + 4096 - (BYTES)) : "memory"); MIXV(o_); report(NAME); } while (0)
        MEMOP("mem-pmovzxbw", "pmovzxbw (%2), %0", 8); MEMOP("mem-pmovsxbd", "pmovsxbd (%2), %0", 4); MEMOP("mem-pmovsxbq", "pmovsxbq (%2), %0", 2);
        MEMOP("mem-pmovzxwd", "pmovzxwd (%2), %0", 8); MEMOP("mem-pmovsxwq", "pmovsxwq (%2), %0", 4); MEMOP("mem-pmovzxdq", "pmovzxdq (%2), %0", 8);
        MEMOP("mem-insertps", "insertps $0x30, (%2), %0", 4); MEMOP("mem-roundss", "roundss $9, (%2), %0", 4); MEMOP("mem-roundsd", "roundsd $10, (%2), %0", 8);
        MEMOP("mem-movddup", "movddup (%2), %0", 8); MEMOP("mem-pextrb", "pxor %0, %0", 1);
        (void) f; (void) g;
        __m128i q = _mm_loadu_si128((const __m128i*)(pg + 4096 - 16));
        MIXV(_mm_cmpistrm(q, q, 0x40)); report("mem-ok");
    }
    /* SSE4.2 */
    for (int i = 0; i < N; i++) { unsigned c = (unsigned) rnd(); u64 q = rnd(); mixi(_mm_crc32_u8(c, (unsigned char) q)); mixi(_mm_crc32_u16(c, (unsigned short) q));
        mixi(_mm_crc32_u32(c, (unsigned) q)); mixi((long long) _mm_crc32_u64(rnd(), q)); } report("crc32");
    for (int i = 0; i < N; i++) { u64 q = rnd() & rnd(); mixi(_mm_popcnt_u64(q)); mixi(_mm_popcnt_u32((unsigned) q)); } report("popcnt");
#define PRUN(imm) PCMP(imm);
    IMM_LIST(PRUN)
    report("pcmpXstrX");
    /* cmpxchg16b / cmpxchg8b / movnti */
    {
        P16 v = {1, 2}, e = {1, 2}; int r1 = cas16x(&v, &e, 10, 20); printf("cas16 ok:   %d (%llu,%llu) e=(%llu,%llu)\n", r1, v.lo, v.hi, e.lo, e.hi);
        e.lo = 5; e.hi = 6; r1 = cas16x(&v, &e, 30, 40); printf("cas16 fail: %d (%llu,%llu) e=(%llu,%llu)\n", r1, v.lo, v.hi, e.lo, e.hi);
        u64 w = 0x1122334455667788ULL, ex = w; r1 = cas8x(&w, &ex, 0xAABBCCDD11223344ULL); printf("cas8 ok:    %d %016llx\n", r1, w);
        ex = 1; r1 = cas8x(&w, &ex, 2); printf("cas8 fail:  %d %016llx e=%016llx\n", r1, w, ex);
        pthread_t th[4]; for (int i = 0; i < 4; i++) pthread_create(&th[i], NULL, worker, NULL);
        for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
        printf("cas16 threads: %llu %llu\n", ctr.lo, ctr.hi);
        int ni = 0; long long nq = 0; _mm_stream_si32(&ni, 0x1234567); _mm_stream_si64(&nq, 0x123456789abcdefLL); _mm_sfence(); printf("movnti: %x %llx\n", ni, (u64) nq);
    }
    /* xgetbv / rdtscp: must execute; their values are not comparable with a real CPU's */
    {
        unsigned lo, hi, aux; __asm__ __volatile__(".byte 0x0f, 0x01, 0xd0" : "=a"(lo), "=d"(hi) : "c"(0)); printf("xgetbv: x87+sse=%u\n", (lo & 3) == 3);
        __asm__ __volatile__(".byte 0x0f, 0x01, 0xf9" : "=a"(lo), "=d"(hi), "=c"(aux)); u64 t1 = ((u64) hi << 32) | lo;
        __asm__ __volatile__(".byte 0x0f, 0x01, 0xf9" : "=a"(lo), "=d"(hi), "=c"(aux)); u64 t2 = ((u64) hi << 32) | lo; printf("rdtscp: monotonic=%d\n", t2 >= t1);
    }
    strings();
    return bad ? 1 : 0;
}
