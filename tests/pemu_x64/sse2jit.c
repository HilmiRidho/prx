/* SSE2 integer ops and bsf/bsr/tzcnt/lzcnt that the JIT translates natively (hot loops, so they get translated), checked by
 * hashing the results against a real x86-64 CPU (sse2jit.expect). Memory and register operands, zero sources, flags after bsf. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <emmintrin.h>

typedef unsigned long long u64;
static u64 H = 0xcbf29ce484222325ULL;
static void mix(const void*p, int n) { const unsigned char*b = p; for (int i = 0; i < n; i++) { H ^= b[i]; H *= 0x100000001b3ULL; } }
static void report(const char*n) { printf("%-14s %016llx\n", n, H); H = 0xcbf29ce484222325ULL; }
static u64 rs = 0x9E3779B97F4A7C15ULL;
static u64 rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
static __m128i ri(void) {
    unsigned char b[16];
    for (int i = 0; i < 16; i++) { u64 r = rnd(); int k = (int)(r % 8); b[i] = k == 0 ? 0 : k == 1 ? 0x7f : k == 2 ? 0x80 : k == 3 ? 0xff : k == 4 ? 1 : (unsigned char)(r >> 8); }
    return _mm_loadu_si128((const __m128i*) b);
}
#define MIXV(v) do { __m128i t_ = (v); mix(&t_, 16); } while (0)
#define N 3000
#define REG(NAME, EXPR) do { for (int i_ = 0; i_ < N; i_++) { __m128i a = ri(), b = ri(); (void) a; (void) b; MIXV(EXPR); } report(NAME); } while (0)
/* memory-source forms: the second operand is loaded straight from memory */
static __m128i mem[2];
#define MEM(NAME, INSN) do { for (int i_ = 0; i_ < N; i_++) { mem[0] = ri(); __m128i a = ri(); __asm__ volatile(INSN " (%1), %0" : "+x"(a) : "r"(&mem[0]) : "memory"); MIXV(a); } report(NAME); } while (0)

#define BS(NAME, INSN, TYPE, REGFMT) static TYPE NAME(TYPE v, TYPE old, int*zf) { TYPE r = old; unsigned char z; __asm__ volatile(INSN " %2, %0\n\tsetz %1" : "+" REGFMT(r), "=q"(z) : REGFMT(v) : "cc"); *zf = z; return r; }
#define R64(x) "r"(x)
#define R32(x) "r"(x)
BS(bsf64, "bsf", u64, "r") BS(bsr64, "bsr", u64, "r")
static unsigned bsf32(unsigned v, unsigned old, int*zf) { unsigned r = old; unsigned char z; __asm__ volatile("bsf %2, %0\n\tsetz %1" : "+r"(r), "=q"(z) : "r"(v) : "cc"); *zf = z; return r; }
static unsigned bsr32(unsigned v, unsigned old, int*zf) { unsigned r = old; unsigned char z; __asm__ volatile("bsr %2, %0\n\tsetz %1" : "+r"(r), "=q"(z) : "r"(v) : "cc"); *zf = z; return r; }
static u64 tz64(u64 v) { u64 r; __asm__ volatile("rep bsf %1, %0" : "=r"(r) : "r"(v) : "cc"); return r; }
static u64 lz64(u64 v) { u64 r; __asm__ volatile("rep bsr %1, %0" : "=r"(r) : "r"(v) : "cc"); return r; }
static unsigned tz32(unsigned v) { unsigned r; __asm__ volatile("rep bsf %1, %0" : "=r"(r) : "r"(v) : "cc"); return r; }
static unsigned lz32(unsigned v) { unsigned r; __asm__ volatile("rep bsr %1, %0" : "=r"(r) : "r"(v) : "cc"); return r; }
/* bsf whose flags are consumed by the very next instruction, from a memory source */
static int bsf_branch(const u64*p, u64*out) { u64 r = 77; int taken; __asm__ volatile("bsf (%2), %0\n\tmov $1, %1\n\tjz 1f\n\tmov $0, %1\n1:" : "+r"(r), "=r"(taken) : "r"(p) : "cc", "memory"); *out = r; return taken; }

int main(void) {
    REG("pminub", _mm_min_epu8(a, b)); REG("pmaxub", _mm_max_epu8(a, b)); REG("pminsw", _mm_min_epi16(a, b)); REG("pmaxsw", _mm_max_epi16(a, b));
    REG("pavgb", _mm_avg_epu8(a, b)); REG("pavgw", _mm_avg_epu16(a, b));
    REG("paddusb", _mm_adds_epu8(a, b)); REG("paddusw", _mm_adds_epu16(a, b)); REG("psubusb", _mm_subs_epu8(a, b)); REG("psubusw", _mm_subs_epu16(a, b));
    REG("paddsb", _mm_adds_epi8(a, b)); REG("paddsw", _mm_adds_epi16(a, b)); REG("psubsb", _mm_subs_epi8(a, b)); REG("psubsw", _mm_subs_epi16(a, b));
    REG("pcmpgtb", _mm_cmpgt_epi8(a, b)); REG("pcmpgtw", _mm_cmpgt_epi16(a, b)); REG("pcmpgtd", _mm_cmpgt_epi32(a, b)); REG("pmullw", _mm_mullo_epi16(a, b));
    REG("punpcklbw", _mm_unpacklo_epi8(a, b)); REG("punpcklwd", _mm_unpacklo_epi16(a, b)); REG("punpckldq", _mm_unpacklo_epi32(a, b)); REG("punpcklqdq", _mm_unpacklo_epi64(a, b));
    REG("punpckhbw", _mm_unpackhi_epi8(a, b)); REG("punpckhwd", _mm_unpackhi_epi16(a, b)); REG("punpckhdq", _mm_unpackhi_epi32(a, b)); REG("punpckhqdq", _mm_unpackhi_epi64(a, b));
    REG("pshufd-0", _mm_shuffle_epi32(a, 0x00)); REG("pshufd-55", _mm_shuffle_epi32(a, 0x55)); REG("pshufd-aa", _mm_shuffle_epi32(a, 0xAA)); REG("pshufd-ff", _mm_shuffle_epi32(a, 0xFF));
    REG("pshufd-1b", _mm_shuffle_epi32(a, 0x1B)); REG("pshufd-4e", _mm_shuffle_epi32(a, 0x4E)); REG("pshufd-e4", _mm_shuffle_epi32(a, 0xE4));
    MEM("m-pminub", "pminub"); MEM("m-pavgw", "pavgw"); MEM("m-psubusb", "psubusb"); MEM("m-pcmpgtd", "pcmpgtd"); MEM("m-punpcklbw", "punpcklbw"); MEM("m-punpckhqdq", "punpckhqdq");
    MEM("m-paddsw", "paddsw"); MEM("m-pmullw", "pmullw");
    for (int i = 0; i < N; i++) { mem[0] = ri(); __m128i a; __asm__ volatile("pshufd $0xAA, (%1), %0" : "=x"(a) : "r"(&mem[0]) : "memory"); MIXV(a); } report("m-pshufd-aa");

    for (int i = 0; i < N; i++) {
        u64 v = rnd() >> (rnd() % 64); if (i % 11 == 0) v = 0; unsigned w = (unsigned) (rnd() >> (rnd() % 64)); if (i % 13 == 0) w = 0;
        u64 old = 0xDEADBEEF00000000ULL | (unsigned) rnd(); int z;
        u64 r; r = bsf64(v, old, &z); mix(&r, 8); mix(&z, 4); r = bsr64(v, old, &z); mix(&r, 8); mix(&z, 4);
        unsigned r32 = bsf32(w, (unsigned) old, &z); mix(&r32, 4); mix(&z, 4); r32 = bsr32(w, (unsigned) old, &z); mix(&r32, 4); mix(&z, 4);
        r = tz64(v); mix(&r, 8); r = lz64(v); mix(&r, 8); r32 = tz32(w); mix(&r32, 4); r32 = lz32(w); mix(&r32, 4);
        u64 buf = v, o; int t = bsf_branch(&buf, &o); mix(&t, 4); mix(&o, 8);
    }
    report("bsf/bsr/tzcnt");
    return 0;
}
