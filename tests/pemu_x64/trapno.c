/* The ucontext of a synchronous fault carries the x86 trap number and error code: Wine's
 * segv_handler dispatches on REG_TRAPNO (14 = page fault, 13 = #GP) and on REG_ERR bit 1 (write). */
#define _GNU_SOURCE
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <ucontext.h>

static sigjmp_buf jb;
static long trap, err;
static void h(int s, siginfo_t *si, void *u) {
    ucontext_t *uc = u; (void) s; (void) si;
    trap = uc->uc_mcontext.gregs[REG_TRAPNO]; err = uc->uc_mcontext.gregs[REG_ERR];
    siglongjmp(jb, 1);
}
int main(void) {
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = h; sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigaction(SIGSEGV, &sa, NULL);
    volatile char *bad = (volatile char *) 0x10;
    char *ro = mmap(0, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (!sigsetjmp(jb, 1)) { (void) *bad; }
    printf("read unmapped: trap=%ld write=%ld\n", trap, (err >> 1) & 1);
    if (!sigsetjmp(jb, 1)) { *bad = 1; }
    printf("write unmapped: trap=%ld write=%ld\n", trap, (err >> 1) & 1);
    if (!sigsetjmp(jb, 1)) { *(volatile char *) ro = 1; }
    printf("write read-only: trap=%ld write=%ld\n", trap, (err >> 1) & 1);
    if (!sigsetjmp(jb, 1)) { __asm__ volatile("cli"); }
    printf("cli: trap=%ld\n", trap);
    return 0;
}
