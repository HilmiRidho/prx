/* x87 state: fxsave/fxrstor with ST(0..7), the x87 stack across a signal handler, fnsave/frstor, fisttp, fbld/fbstp.
 * Values are exactly representable, so the output is the same with 80-bit and with quad long double. */
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>

static unsigned char fx[512] __attribute__((aligned(16)));
static unsigned char sv[108];

static void hnd(int s) {                      /* clobbers the x87 stack: the kernel/emulator must save and restore it */
    (void) s;
    long double x = 100.0L, y = 7.0L;
    __asm__ __volatile__("fldt %0; fldt %1; faddp; fstpt %0" : "+m"(x) : "m"(y) : "st");
    __asm__ __volatile__("fninit");
}
static void show(const char*n, long double v) { printf("%s=%.4Lf\n", n, v); }

int main(void) {
    long double a = 1.5L, b = 2.25L, c = -3.0L, r0, r1, r2;

    /* fxsave / fxrstor keep ST(0..7) and TOP */
    __asm__ __volatile__("fldt %0; fldt %1; fldt %2" :: "m"(a), "m"(b), "m"(c));          /* ST0=c ST1=b ST2=a */
    __asm__ __volatile__("fxsave %0" : "=m"(fx) :: "memory");
    __asm__ __volatile__("fninit");
    printf("fsw.top after fxsave: %u\n", (fx[3] >> 3) & 7);
    long double s0, s1, s2; memcpy(&s0, fx + 32, 10); memcpy(&s1, fx + 48, 10); memcpy(&s2, fx + 64, 10);
    show("image.st0", s0); show("image.st1", s1); show("image.st2", s2);
    __asm__ __volatile__("fxrstor %0" :: "m"(fx) : "memory");
    __asm__ __volatile__("fstpt %0; fstpt %1; fstpt %2" : "=m"(r0), "=m"(r1), "=m"(r2));
    show("fxrstor.st0", r0); show("fxrstor.st1", r1); show("fxrstor.st2", r2);

    /* the stack survives a signal handler that uses the x87 unit */
    signal(SIGUSR1, hnd);
    __asm__ __volatile__("fldt %0; fldt %1; fldt %2" :: "m"(a), "m"(b), "m"(c));
    raise(SIGUSR1);
    __asm__ __volatile__("fstpt %0; fstpt %1; fstpt %2" : "=m"(r0), "=m"(r1), "=m"(r2));
    show("signal.st0", r0); show("signal.st1", r1); show("signal.st2", r2);

    /* fnsave / frstor */
    __asm__ __volatile__("fldt %0; fldt %1" :: "m"(a), "m"(b));                           /* ST0=b ST1=a */
    __asm__ __volatile__("fnsave %0" : "=m"(sv) :: "memory");
    unsigned short cw; __asm__ __volatile__("fnstcw %0" : "=m"(cw));
    printf("fcw after fnsave: %04x\n", cw);
    memcpy(&s0, sv + 28, 10); memcpy(&s1, sv + 38, 10);
    show("save.st0", s0); show("save.st1", s1);
    __asm__ __volatile__("frstor %0" :: "m"(sv) : "memory");
    __asm__ __volatile__("fstpt %0; fstpt %1" : "=m"(r0), "=m"(r1));
    show("frstor.st0", r0); show("frstor.st1", r1);

    /* fisttp truncates whatever the rounding mode is */
    { long double v = -2.75L, w = 3.875L; int i32; long long i64; short i16;
      __asm__ __volatile__("fldt %1; fisttpl %0" : "=m"(i32) : "m"(v));
      __asm__ __volatile__("fldt %1; fisttpll %0" : "=m"(i64) : "m"(w));
      __asm__ __volatile__("fldt %1; fisttps %0" : "=m"(i16) : "m"(w));
      printf("fisttp: %d %lld %d\n", i32, i64, i16); }

    /* packed BCD */
    { unsigned char bcd[10]; long double v = -1234567890123456.0L, back;
      __asm__ __volatile__("fldt %1; fbstp %0" : "=m"(bcd) : "m"(v) : "st");
      for (int i = 9; i >= 0; i--) printf("%02x", bcd[i]);
      __asm__ __volatile__("fbld %1; fstpt %0" : "=m"(back) : "m"(bcd) : "st");
      printf("\nfbld back: %.1Lf\n", back);
      long double h = 12345.5L; __asm__ __volatile__("fldt %1; fbstp %0" : "=m"(bcd) : "m"(h) : "st");
      for (int i = 9; i >= 0; i--) printf("%02x", bcd[i]);
      printf("\n"); }
    return 0;
}
