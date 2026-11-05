/* JIT: 66 0f b6/be (movzx/movsx with a 16-bit destination) and inc/dec (fe/ff /0 /1) in every size, register and memory form,
 * with the flags dead (a later add overwrites them: translated) and live (read straight away: interpreter, CF must pass through).
 * Every group runs in a loop so the blocks get translated. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef uint64_t u64; typedef uint32_t u32; typedef uint16_t u16; typedef uint8_t u8;
static u64 seed = 0x9E3779B97F4A7C15ULL;
static inline __attribute__((always_inline)) u64 rnd(void) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed; }
static u64 edge(int i) {
    static const u64 e[] = { 0, 1, 0x7f, 0x80, 0xff, 0x7fff, 0x8000, 0xffff, 0x7fffffffULL, 0x80000000ULL, 0xffffffffULL, 0x7fffffffffffffffULL,
                             0x8000000000000000ULL, ~0ULL, 0xfe, 0x7ffe, 0xfffe, 0x7ffffffeULL, 0xfffffffeULL, 2 };
    return i < 20 ? e[i] : rnd();
}
static u64 sum;
static inline __attribute__((always_inline)) void mix(u64 v) { sum = sum * 1000003ULL + v; sum ^= sum >> 29; }
static volatile u64 mem64[2];

/* flags dead: inc/dec, then an add rewrites all the flags, then CF of the add is read */
#define DEAD_REG(ins, sfx, m) do { u64 x = edge(i), y = rnd(), z = rnd(); u8 c; \
    __asm__ volatile(ins #sfx " %" #m "0\n\t" "addl %k3, %k1\n\t" "setc %2" : "+q"(x), "+r"(y), "=q"(c) : "r"(z) : "cc"); \
    mix(x); mix(y); mix(c); } while (0)
#define DEAD_MEM(ins, sfx, T) do { u64 y = rnd(), z = rnd(); u8 c; mem64[0] = edge(i); \
    __asm__ volatile(ins #sfx " %0\n\t" "addl %k3, %k1\n\t" "setc %2" : "+m"(*(volatile T*) &mem64[0]), "+r"(y), "=q"(c) : "r"(z) : "cc"); \
    mix(mem64[0]); mix(y); mix(c); } while (0)
/* flags live: with CF forced to 1 or 0 before, all the flags are read after */
#define LIVE_REG(ins, sfx, m, pre) do { u64 x = edge(i); u8 c, zf, of, sf; \
    __asm__ volatile(pre "\n\t" ins #sfx " %" #m "0\n\t" "setc %1\n\t" "setz %2\n\t" "seto %3\n\t" "sets %4" \
                     : "+q"(x), "=q"(c), "=q"(zf), "=q"(of), "=q"(sf) :: "cc"); \
    mix(x); mix(c | zf << 1 | of << 2 | sf << 3); } while (0)
#define LIVE_MEM(ins, sfx, T, pre) do { u8 c, zf, of, sf; mem64[0] = edge(i); \
    __asm__ volatile(pre "\n\t" ins #sfx " %0\n\t" "setc %1\n\t" "setz %2\n\t" "seto %3\n\t" "sets %4" \
                     : "+m"(*(volatile T*) &mem64[0]), "=q"(c), "=q"(zf), "=q"(of), "=q"(sf) :: "cc"); \
    mix(mem64[0]); mix(c | zf << 1 | of << 2 | sf << 3); } while (0)

#define ALL_DEAD(ins) \
    DEAD_REG(ins, b, b); DEAD_REG(ins, w, w); DEAD_REG(ins, l, k); DEAD_REG(ins, q, q); \
    DEAD_MEM(ins, b, u8); DEAD_MEM(ins, w, u16); DEAD_MEM(ins, l, u32); DEAD_MEM(ins, q, u64)
#define ALL_LIVE(ins, pre) \
    LIVE_REG(ins, b, b, pre); LIVE_REG(ins, w, w, pre); LIVE_REG(ins, l, k, pre); LIVE_REG(ins, q, q, pre); \
    LIVE_MEM(ins, b, u8, pre); LIVE_MEM(ins, w, u16, pre); LIVE_MEM(ins, l, u32, pre); LIVE_MEM(ins, q, u64, pre)

int main(void) {
    for (int i = 0; i < 3000; i++) { ALL_DEAD("inc"); ALL_DEAD("dec"); }
    printf("incdec dead %016llx\n", (unsigned long long) sum);
    sum = 0;
    for (int i = 0; i < 3000; i++) { ALL_LIVE("inc", "stc"); ALL_LIVE("dec", "stc"); ALL_LIVE("inc", "clc"); ALL_LIVE("dec", "clc"); }
    printf("incdec live %016llx\n", (unsigned long long) sum);

    sum = 0;
    for (int i = 0; i < 3000; i++) {
        u64 s = edge(i % 25), d = rnd(), r;
        static volatile u8 b8[8]; b8[0] = (u8) s;
        r = d; __asm__ volatile("movsbw %b1, %w0" : "+r"(r) : "q"(s)); mix(r);
        r = d; __asm__ volatile("movzbw %b1, %w0" : "+r"(r) : "q"(s)); mix(r);
        r = d; __asm__ volatile("movsbw %1, %w0" : "+r"(r) : "m"(b8[0])); mix(r);
        r = d; __asm__ volatile("movzbw %1, %w0" : "+r"(r) : "m"(b8[0])); mix(r);
        r = d; __asm__ volatile("movsbw %%ah, %%cx" : "+c"(r) : "a"(s)); mix(r);
        r = d; __asm__ volatile("movzbw %%ah, %%cx" : "+c"(r) : "a"(s)); mix(r);
        r = d; __asm__ volatile("movsbw %%al, %%ax" : "+a"(r)); mix(r);
        r = d; __asm__ volatile("movzbw %%al, %%ax" : "+a"(r)); mix(r);
        r = d; __asm__ volatile("movsbl %b1, %k0" : "+r"(r) : "q"(s)); mix(r);           /* neighbours that were already translated */
        r = d; __asm__ volatile("movzbq %b1, %q0" : "+r"(r) : "q"(s)); mix(r);
    }
    printf("movx16 %016llx\n", (unsigned long long) sum);
    return 0;
}
