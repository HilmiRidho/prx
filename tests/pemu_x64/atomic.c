/* lock-prefixed read-modify-write and xchg must be atomic between guest threads (pemu_x64 runs each on its own host thread) */
#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
#define N 40000
static volatile long add_c, dec_c, xadd_c, cas_c, or_c;
static volatile unsigned bits;
static volatile int lockw, locked_c;
static volatile int8_t b8;
static volatile int16_t w16;
static void*w(void*a){
  (void) a;
  for (int i = 0; i < N; i++) {
    __sync_fetch_and_add(&add_c, 1);                         /* lock add / lock xadd */
    __asm__ volatile("lock decq %0" : "+m"(dec_c));          /* lock dec */
    __sync_fetch_and_add(&xadd_c, 3);
    long o; do { o = cas_c; } while (!__sync_bool_compare_and_swap(&cas_c, o, o + 1));   /* lock cmpxchg */
    __sync_fetch_and_add(&b8, 1);                            /* 8-bit lock xadd */
    __sync_fetch_and_add(&w16, 1);                           /* 16-bit lock add */
    __asm__ volatile("lock btsl $3, %0" : "+m"(bits) :: "cc");   /* lock bts */
    __asm__ volatile("lock orq $1, %0" : "+m"(or_c));        /* lock or imm */
    while (__sync_lock_test_and_set(&lockw, 1)) { }          /* xchg mem: spin lock protects a plain counter */
    locked_c = locked_c + 1;
    __sync_lock_release(&lockw);
  }
  return 0;
}
int main(void){
  pthread_t t[4];
  for (int i = 0; i < 4; i++) pthread_create(&t[i], 0, w, 0);
  for (int i = 0; i < 4; i++) pthread_join(t[i], 0);
  printf("add=%ld dec=%ld xadd=%ld cas=%ld\n", add_c, dec_c, xadd_c, cas_c);
  printf("b8=%d w16=%d bits=%u or=%ld locked=%d\n", (int) b8, (int) w16, bits, or_c, locked_c);
  /* single-thread semantics of the atomic forms: result and flags */
  long x = 5, y = 0; unsigned char zf, cf;
  __asm__ volatile("lock cmpxchgq %3, %0; sete %1" : "+m"(x), "=r"(zf), "+a"(y) : "r"(9L) : "cc");
  printf("cmpxchg fail: x=%ld rax=%ld zf=%d\n", x, y, (int) zf);
  y = 5; __asm__ volatile("lock cmpxchgq %3, %0; sete %1" : "+m"(x), "=r"(zf), "+a"(y) : "r"(9L) : "cc");
  printf("cmpxchg ok: x=%ld zf=%d\n", x, (int) zf);
  unsigned v = 0x10; __asm__ volatile("lock btrl $4, %0; setc %1" : "+m"(v), "=r"(cf) :: "cc");
  printf("btr: v=%u cf=%d\n", v, (int) cf);
  long n = 7; __asm__ volatile("lock negq %0" : "+m"(n) :: "cc"); printf("neg=%ld\n", n);
  long q = 0; __asm__ volatile("lock notq %0" : "+m"(q) :: "cc"); printf("not=%ld\n", q);
  int xa = 40, r = 2; __asm__ volatile("lock xaddl %1, %0" : "+m"(xa), "+r"(r) :: "cc"); printf("xadd: mem=%d reg=%d\n", xa, r);
  return 0;
}
