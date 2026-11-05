/* JIT: andps/andnps/orps/xorps and the pd forms, register and memory operands, in a loop so the blocks get translated. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
typedef uint64_t u64;
static u64 seed = 0x2545F4914F6CDD1DULL;
static inline __attribute__((always_inline)) u64 rnd(void) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed; }
static u64 sum;
static inline __attribute__((always_inline)) void mix(u64 v) { sum = sum * 1000003ULL + v; sum ^= sum >> 29; }
typedef struct { u64 q[2]; } v128 __attribute__((aligned(16)));
#define OP(ins) do { v128 a, b, c; a.q[0] = rnd(); a.q[1] = rnd(); b.q[0] = rnd(); b.q[1] = rnd(); c = a; \
    __asm__ volatile("movups %1, %%xmm0\n\tmovups %2, %%xmm1\n\t" ins " %%xmm1, %%xmm0\n\tmovups %%xmm0, %0" : "=m"(c) : "m"(a), "m"(b) : "xmm0", "xmm1"); \
    mix(c.q[0]); mix(c.q[1]); \
    __asm__ volatile("movups %1, %%xmm0\n\t" ins " %2, %%xmm0\n\tmovups %%xmm0, %0" : "=m"(c) : "m"(a), "m"(b) : "xmm0"); \
    mix(c.q[0]); mix(c.q[1]); } while (0)
int main(void) {
    for (int i = 0; i < 2000; i++) { OP("andps"); OP("andnps"); OP("orps"); OP("xorps"); OP("andpd"); OP("andnpd"); OP("orpd"); OP("xorpd"); }
    printf("xlogps %016llx\n", (unsigned long long) sum);
    return 0;
}
