/* minsd/minss/maxsd/maxss (NaN, +0/-0, infinities, memory source) and cvtss2sd/cvtsd2ss (NaN payloads, denormals, overflow,
 * the untouched upper part of the destination). The expected output is what a real x86-64 prints. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <emmintrin.h>

typedef union { double d; float f; uint64_t u; uint32_t w; } V;
static const uint64_t D[] = { 0x0000000000000000ULL, 0x8000000000000000ULL, 0x3ff0000000000000ULL, 0xbff0000000000000ULL, 0x4000000000000000ULL,
    0x7ff0000000000000ULL, 0xfff0000000000000ULL, 0x7ff8000000000000ULL, 0x7ff0000000000001ULL, 0xfff8000000000123ULL, 0x0000000000000001ULL,
    0x47efffffe0000000ULL, 0x47f0000000000000ULL, 0x36a0000000000000ULL, 0x3690000000000000ULL, 0xc3e0000000000000ULL };
#define ND (sizeof D / sizeof D[0])

#define OPD(name, ins) \
static uint64_t name(uint64_t a, uint64_t b) { __m128i x = _mm_set_epi64x(0x1111222233334444LL, (long long) a), y = _mm_set_epi64x(0x5555666677778888LL, (long long) b); \
    __asm__ volatile(ins " %[y], %[x]" : [x]"+x"(x) : [y]"x"(y)); \
    uint64_t lo = (uint64_t) _mm_cvtsi128_si64(x), hi = (uint64_t) _mm_cvtsi128_si64(_mm_unpackhi_epi64(x, x)); return lo ^ (hi * 31); } \
static uint64_t name##m(uint64_t a, uint64_t b) { __m128i x = _mm_set_epi64x(0x1111222233334444LL, (long long) a); \
    __asm__ volatile(ins " (%[p]), %[x]" : [x]"+x"(x) : [p]"r"(&b) : "memory"); \
    uint64_t lo = (uint64_t) _mm_cvtsi128_si64(x), hi = (uint64_t) _mm_cvtsi128_si64(_mm_unpackhi_epi64(x, x)); return lo ^ (hi * 31); }
OPD(t_minsd, "minsd") OPD(t_maxsd, "maxsd") OPD(t_minss, "minss") OPD(t_maxss, "maxss") OPD(t_ss2sd, "cvtss2sd") OPD(t_sd2ss, "cvtsd2ss")

int main(void) {
    uint64_t h = 1469598103934665603ULL;
#define MIX(v) do { h ^= (v); h *= 1099511628211ULL; } while (0)
    for (unsigned i = 0; i < ND; i++) for (unsigned j = 0; j < ND; j++) {
        uint64_t a = D[i], b = D[j];
        uint64_t r[12] = { t_minsd(a, b), t_minsdm(a, b), t_maxsd(a, b), t_maxsdm(a, b), t_minss(a, b), t_minssm(a, b), t_maxss(a, b), t_maxssm(a, b),
                           t_ss2sd(a, b), t_ss2sdm(a, b), t_sd2ss(a, b), t_sd2ssm(a, b) };
        for (int k = 0; k < 12; k++) MIX(r[k]);
        if (i < 6 && j < 6) printf("%u %u: %016llx %016llx %016llx %016llx %016llx %016llx\n", i, j, (unsigned long long) r[0], (unsigned long long) r[2],
               (unsigned long long) r[4], (unsigned long long) r[6], (unsigned long long) r[8], (unsigned long long) r[10]);
    }
    printf("hash %016llx\n", (unsigned long long) h);
    return 0;
}
