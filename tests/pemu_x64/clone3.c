/* clone3: a fork-style child, a thread started on its own stack (the way glibc 2.34+ does it), CLONE_PIDFD, and the argument checks. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <linux/futex.h>

struct ca { uint64_t flags, pidfd, child_tid, parent_tid, exit_signal, stack, stack_size, tls, set_tid, set_tid_size, cgroup; };
static void res(const char*n, int ok) { printf("%s: %s\n", n, ok ? "ok" : "FAIL"); }

static volatile int tidvar, shared;
static char stk[64 * 1024] __attribute__((aligned(16)));
static int thr(void*p) { *(volatile int*) p = 42; return 0; }

static long clone3_run(struct ca*a, int (*fn)(void*), void*arg) {
    long ret;
    register long r12 __asm__("r12") = (long) fn;
    register long r13 __asm__("r13") = (long) arg;
    __asm__ volatile(
        "syscall\n\t"
        "test %%rax, %%rax\n\t"
        "jnz 1f\n\t"
        "xor %%ebp, %%ebp\n\t"
        "mov %%r13, %%rdi\n\t"
        "call *%%r12\n\t"
        "mov %%eax, %%edi\n\t"
        "mov $60, %%eax\n\t"
        "syscall\n\t"
        "1:\n\t"
        : "=a"(ret) : "0"(435L), "D"(a), "S"(sizeof *a), "r"(r12), "r"(r13) : "rcx", "r11", "memory");
    return ret;
}

int main(void) {
    struct ca a; long r; int st;

    memset(&a, 0, sizeof a); a.exit_signal = SIGCHLD;
    r = syscall(SYS_clone3, &a, sizeof a);
    if (r == 0) { static const char m[] = "child runs\n"; write(1, m, sizeof m - 1); _exit(7); }
    res("fork-style clone3", r > 0 && waitpid((pid_t) r, &st, 0) == r && WIFEXITED(st) && WEXITSTATUS(st) == 7);

    memset(&a, 0, sizeof a);
    a.flags = 0x100 | 0x200 | 0x400 | 0x800 | 0x10000 | 0x40000 | 0x00100000 | 0x00200000;   /* VM FS FILES SIGHAND THREAD SYSVSEM PARENT_SETTID CHILD_CLEARTID */
    a.stack = (uint64_t)(uintptr_t) stk; a.stack_size = sizeof stk;
    a.parent_tid = a.child_tid = (uint64_t)(uintptr_t) &tidvar;
    r = clone3_run(&a, thr, (void*) &shared);
    int t, spins = 0;
    while (r > 0 && (t = tidvar) != 0 && spins++ < 100000) syscall(SYS_futex, &tidvar, FUTEX_WAIT, t, 0, 0, 0);
    res("thread clone3", r > 0 && tidvar == 0 && shared == 42);

    memset(&a, 0, sizeof a); a.exit_signal = SIGCHLD; a.flags = 0x1000; int pfd = -1; a.pidfd = (uint64_t)(uintptr_t) &pfd;
    r = syscall(SYS_clone3, &a, sizeof a);
    if (r == 0) _exit(0);
    if (r > 0) { waitpid((pid_t) r, &st, 0); res("pidfd", pfd >= 0); }
    else res("pidfd", errno == ENOSYS || errno == EPERM || errno == EINVAL);          /* the host kernel has no pidfd */

    memset(&a, 0, sizeof a); a.exit_signal = SIGCHLD;
    errno = 0; r = syscall(SYS_clone3, &a, 8);                      res("small size EINVAL", r == -1 && errno == EINVAL);
    a.flags = 0x20000;                                              /* CLONE_NEWNS */
    errno = 0; r = syscall(SYS_clone3, &a, sizeof a);               res("newns EPERM", r == -1 && errno == EPERM);
    a.flags = 1ULL << 32;                                           /* CLONE_CLEAR_SIGHAND */
    errno = 0; r = syscall(SYS_clone3, &a, sizeof a);               res("clear_sighand EINVAL", r == -1 && errno == EINVAL);
    a.flags = 0; a.stack = 0x1000;                                  /* stack without stack_size */
    errno = 0; r = syscall(SYS_clone3, &a, sizeof a);               res("stack without size EINVAL", r == -1 && errno == EINVAL);
    return 0;
}
