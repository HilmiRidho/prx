/* sys.h - raw syscall layer (sc), 64-bit div helpers, pread_
 * Part of the prx shim (unity build: included by src/prx.c, not a standalone header). */
#if defined(__aarch64__)
static inline long sc(long n, long a, long b, long c, long d, long e, long f) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;
    __asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory", "cc");
    return x0;
}
#elif defined(__i386__)
/* int 0x80 with up to six arguments. ebx (PIC register) and ebp are saved by
   hand; the arguments are loaded from a stack array through eax. */
static inline long sc(long n, long a, long b, long c, long d, long e, long f) {
    long v[7] = {n, a, b, c, d, e, f};
    long ret;
    __asm__ volatile(
        "push %%ebx\n\t"
        "push %%ebp\n\t"
        "mov 4(%%eax), %%ebx\n\t"
        "mov 8(%%eax), %%ecx\n\t"
        "mov 12(%%eax), %%edx\n\t"
        "mov 16(%%eax), %%esi\n\t"
        "mov 20(%%eax), %%edi\n\t"
        "mov 24(%%eax), %%ebp\n\t"
        "mov (%%eax), %%eax\n\t"
        "int $0x80\n\t"
        "pop %%ebp\n\t"
        "pop %%ebx"
        : "=a"(ret) : "a"(v) : "ecx", "edx", "esi", "edi", "memory", "cc");
    return ret;
}
#else
static inline long sc(long n, long a, long b, long c, long d, long e, long f) {
    long ret;
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return ret;
}
#endif
#if defined(__i386__)
/* The shim is linked with -nostdlib, so the 64-bit division/modulo helpers the
   compiler emits on 32-bit x86 are provided here (shift/subtract, only used for
   a few rare formatting paths). */
static u64 udivmod64(u64 n, u64 d, u64*rem) {
    u64 q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r<<1)|((n>>i)&1);
        if (r >= d) {
            r -= d;
            q |= (u64)1<<i;
        }
    }
    if (rem)*rem = r;
    return q;
}
__attribute__((visibility("hidden"))) u64 __udivdi3(u64 a, u64 b) {
    return udivmod64(a, b, 0);
}
__attribute__((visibility("hidden"))) u64 __umoddi3(u64 a, u64 b) {
    u64 r;
    udivmod64(a, b, &r);
    return r;
}
#endif
#define SC3(n,a,b,c) sc((n),(long)(a),(long)(b),(long)(c),0,0,0)
#define SC4(n,a,b,c,d) sc((n),(long)(a),(long)(b),(long)(c),(long)(d),0,0)
#define SC5(n,a,b,c,d,e) sc((n),(long)(a),(long)(b),(long)(c),(long)(d),(long)(e),0)
static long pread_(long fd, void*b, long n, u64 off) {
#if defined(__i386__)
    return sc(N_pread64, fd, (long) b, n, (long)(u32) off, (long)(u32)(off>>32), 0);
#else
    return sc(N_pread64, fd, (long) b, n, (long) off, 0, 0);
#endif
}
