/* JIT-specific checks: async signal in a busy loop, a resumable fault in the middle of a block (exact flags and
 * registers), code rewritten after it ran (mprotect), self-modifying code on a writable+executable page,
 * and an indirect call table. Works the same natively and under every pemu engine. */
#define _GNU_SOURCE
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <ucontext.h>

static volatile int stop;
static void on_alrm(int s) { (void) s; stop = 1; }
static void on_segv(int s, siginfo_t *si, void *u) {
    ucontext_t *uc = u; (void) s; (void) si;
    uc->uc_mcontext.gregs[REG_RIP] += 2;               /* skip "mov (%rax),%ecx" */
    uc->uc_mcontext.gregs[REG_RCX] = 77;
}
static int add1(int x) { return x + 1; }
static int add2(int x) { return x + 2; }
static int mul3(int x) { return x * 3; }

int main(void) {
    /* 1: pending signal must interrupt a loop with no syscalls */
    signal(SIGALRM, on_alrm);
    struct itimerval it = {{0, 0}, {0, 30000}};
    setitimer(ITIMER_REAL, &it, 0);
    unsigned long n = 0;
    while (!stop) n++;
    printf("loop interrupted=%d\n", n > 0);

    /* 2: fault inside a block after a flag-setting instruction; the handler resumes after the faulting load */
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = on_segv; sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigaction(SIGSEGV, &sa, 0);
    unsigned long rcx_out, flags_out; unsigned char cf_out;
    __asm__ volatile(
        "mov $1, %%rdx\n"
        "xor %%ecx, %%ecx\n"
        "cmp $5, %%rdx\n"                 /* CF=1, SF=1 */
        "mov (%%rax), %%ecx\n"            /* faults; handler skips it and sets rcx=77 */
        "setb %%bl\n"
        "pushfq\n pop %%rsi\n"
        : "=c"(rcx_out), "=b"(cf_out), "=S"(flags_out)
        : "a"((void*) 0) : "rdx", "cc", "memory");
    printf("fault: rcx=%lu cf=%d flags=%#lx\n", rcx_out, cf_out & 1, flags_out & 0x8d5);

    /* 3: generated code is overwritten and called again after mprotect (cache invalidation) */
    unsigned char *pg = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int total = 0;
    for (int v = 1; v <= 3; v++) {
        mprotect(pg, 4096, PROT_READ | PROT_WRITE);
        unsigned char code[] = { 0xb8, 0, 0, 0, 0, 0xc3 };      /* mov eax, imm32 ; ret */
        code[1] = (unsigned char)(v * 10);
        memcpy(pg, code, sizeof code);
        mprotect(pg, 4096, PROT_READ | PROT_EXEC);
        for (int k = 0; k < 4; k++) total += ((int (*)(void)) pg)();
    }
    printf("rewritten code total=%d\n", total);

    /* 4: self-modifying code on a writable and executable page (never cached by the JIT) */
    unsigned char *rwx = mmap(0, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int sm = 0;
    for (int v = 1; v <= 3; v++) {
        unsigned char code[] = { 0xb8, 0, 0, 0, 0, 0xc3 };
        code[1] = (unsigned char)(v * 7);
        memcpy(rwx, code, sizeof code);
        for (int k = 0; k < 3; k++) sm += ((int (*)(void)) rwx)();
    }
    printf("self-modifying total=%d\n", sm);

    /* 5: indirect calls through a table */
    int (*tab[3])(int) = { add1, add2, mul3 };
    int acc = 1;
    for (int i = 0; i < 30; i++) acc = tab[i % 3](acc) & 0xffff;
    printf("indirect acc=%d\n", acc);
    return 0;
}
