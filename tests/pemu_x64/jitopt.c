/* Checks for register pinning, superblocks, the return stack buffer and the multithreaded JIT: deep and unbalanced
 * call/ret (recursion, longjmp), high-byte registers, a fault and an async signal while registers are pinned, and guest
 * threads running hot loops while another thread keeps invalidating the code cache (mprotect on translated code). */
#define _GNU_SOURCE
#include <stdio.h>
#include <setjmp.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <ucontext.h>

static __attribute__((noinline)) long fib(long n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
static __attribute__((noinline)) long deep(long n) { return n == 0 ? 0 : 1 + deep(n - 1); }       /* deeper than the return stack buffer */
static jmp_buf jb;
static __attribute__((noinline)) void thrower(int n) { if (n == 0) longjmp(jb, 7); thrower(n - 1); }
static volatile int stop;
static void on_alrm(int s) { (void) s; stop = 1; }
static void on_segv(int s, siginfo_t *si, void *u) {
    ucontext_t *uc = u; (void) s; (void) si;
    uc->uc_mcontext.gregs[REG_RIP] += 2;               /* skip "mov (%rax),%ebx" */
    uc->uc_mcontext.gregs[REG_RBX] = uc->uc_mcontext.gregs[REG_RSI] + 5;
}
static int sel(int x) { switch (x & 3) { case 0: return 11; case 1: return 22; case 2: return 33; default: return 44; } }

static volatile int mt_stop;
static void *worker(void *a) {
    long id = (long) a, acc = 0;
    for (int k = 0; k < 2000; k++) { acc += fib(8 + (long) (k & 3)) + sel(k + (int) id) + deep(10); if (mt_stop) break; }
    return (void *) acc;
}
static void *flusher(void *a) {
    unsigned char *pg = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    unsigned char code[] = { 0xb8, 0, 0, 0, 0, 0xc3 };
    long t = 0; (void) a;
    for (int v = 1; v <= 150; v++) {
        mprotect(pg, 4096, PROT_READ | PROT_WRITE);
        code[1] = (unsigned char) v; memcpy(pg, code, sizeof code);
        mprotect(pg, 4096, PROT_READ | PROT_EXEC);
        t += ((int (*)(void)) pg)();
    }
    return (void *) t;
}

int main(void) {
    printf("fib(20)=%ld deep(500)=%ld\n", fib(20), deep(500));
    int r = setjmp(jb);
    if (!r) thrower(40);
    printf("longjmp=%d after=%ld %ld\n", r, fib(15), deep(200));

    /* high-byte and partial registers (pinned rax..rdi) */
    unsigned long x = 0x1122334455667788UL, y;
    __asm__ volatile("mov %1, %%rax\n mov %%ah, %%bl\n mov %%al, %%ch\n mov %%ax, %%dx\n movzx %%ch, %%esi\n movsx %%bl, %%rdi\n"
                     "mov %%rax, %0\n add %%rsi, %0\n add %%rdi, %0\n add %%rdx, %0\n"
                     : "=r"(y) : "r"(x) : "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "cc");
    printf("partial=%#lx\n", y);

    /* fault with pinned registers live: rsi + 5 must reach the handler's view of rbx */
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = on_segv; sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigaction(SIGSEGV, &sa, 0);
    unsigned long rbx_out, rdi_out;
    __asm__ volatile("mov $1000, %%rsi\n mov $77, %%rdi\n mov (%%rax), %%ebx\n" : "=b"(rbx_out), "=D"(rdi_out) : "a"((void *) 0) : "rsi", "memory");
    printf("fault rbx=%lu rdi=%lu\n", rbx_out, rdi_out);

    /* async signal in a call-heavy loop */
    signal(SIGALRM, on_alrm);
    struct itimerval it = {{0, 0}, {0, 30000}}; setitimer(ITIMER_REAL, &it, 0);
    long n = 0; while (!stop) n += fib(5);
    printf("alarm interrupted=%d\n", n > 0);

    pthread_t t[4], f; long sum = 0, tot = 0;
    for (long i = 0; i < 4; i++) pthread_create(&t[i], 0, worker, (void *) i);
    pthread_create(&f, 0, flusher, 0);
    for (int i = 0; i < 4; i++) { void *v; pthread_join(t[i], &v); sum += (long) v; }
    { void *v; pthread_join(f, &v); tot = (long) v; }
    printf("threads sum=%ld flusher=%ld\n", sum, tot);
    return 0;
}
