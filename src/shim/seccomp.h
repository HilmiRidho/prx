/* seccomp.h - workarounds for a host seccomp policy (Android): SIGSYS -> ENOSYS
 * Part of the prx shim (unity build: included by src/prx.c, not a standalone header).
 *
 * The host's filter kills (SIGSYS) or rejects syscalls glibc uses but bionic does
 * not (rseq, clone3, faccessat2, ...). This module:
 *   - installs a SIGSYS handler that turns a blocked syscall into -ENOSYS, so glibc
 *     takes its fallback path;
 *   - hides signal 31 from the guest (sigaction/signal/sigprocmask) so the handler
 *     is not replaced or blocked;
 *   - papers over blocked sched_{get,set}affinity and PR_SET_NO_NEW_PRIVS.
 * (The matching loader/libc patch and GLIBC_TUNABLES default live in the `prx`
 * launcher and in exec.h.)
 *
 * Switches:  -DPRX_NO_SECCOMP at build time (PRX_SHIM_CFLAGS=-DPRX_NO_SECCOMP prx build)
 *            PRX_SECCOMP=0 at run time (prx login --no-seccomp)               */
static int g_seccomp = 1;
#ifdef PRX_NO_SECCOMP
#define SECCOMP_ON() 0
/* No override in this build: declare libc's, used by fake_proc_stat() (procfs.h).
   Without it clang (C99+) rejects the implicit declaration and the shim fails to build. */
extern int sched_getaffinity(int, size_t, void*);
static void seccomp_init(void) {
}
static long seccomp_nnp(int(*f)(int, unsigned long, unsigned long, unsigned long, unsigned long), unsigned long a2, unsigned long a3, unsigned long a4, unsigned long a5) {
    return f ? f(38, a2, a3, a4, a5) : 0;
}
#else
#define SECCOMP_ON() (g_seccomp)
extern int sigaction(int, const void*, void*);
static int g_inst;
#if defined(__aarch64__)
#define UC_RET_OFF 184
#elif defined(__i386__)
#define UC_RET_OFF 0
#else
#define UC_RET_OFF 144
#endif
/* Must have exactly the size of the guest libc's `struct sigaction` (152 bytes
   on x86_64/aarch64, 140 on i386): sigaction() below zeroes the caller's `old`
   buffer with sizeof (struct ksa). An explicit pad field made it 144 on i386,
   overrunning the caller's buffer by 4 bytes ("stack smashing detected"). */
struct ksa {
    void*h;
    u8 mask[128];
    int flags;
    void*rst;
};
#if defined(__i386__)
_Static_assert(sizeof (struct ksa) == 140, "struct ksa must match i386 struct sigaction");
#else
_Static_assert(sizeof (struct ksa) == 152, "struct ksa must match 64-bit struct sigaction");
#endif
static void prx_early(void);
EXP int sched_setaffinity(int pid, size_t cpusetsize, const void*mask) {
    prx_early();
    long r = sc(N_sched_setaffinity, pid, (long) cpusetsize, (long) mask, 0, 0, 0);
    if (r >= 0) return 0;
    /* Same policy as sched_getaffinity: only a syscall the host's seccomp
       filter blocks is papered over; genuine errors (EINVAL, ESRCH, ...)
       reach the caller, and --no-seccomp never fakes anything. */
    int blocked = g_seccomp && (r == -ENOSYS || ((r == -EPERM || r == -EACCES) && pid == 0));
    return blocked ? 0 : (int) ret(r);
}
EXP int sched_getaffinity(int pid, size_t cpusetsize, void*mask) {
    prx_early();
    long r = sc(N_sched_getaffinity, pid, (long) cpusetsize, (long) mask, 0, 0, 0);
    u8*m = (u8*) mask;
    if (r < 0) {
        /* Genuine errors (EINVAL: buffer too small, ESRCH: no such pid,
           EFAULT, ...) must reach the caller. Only a syscall that the host's
           seccomp policy blocks (ENOSYS, or EPERM/EACCES for ourselves) is
           papered over with a synthetic mask so nproc & co. keep working. */
        int blocked = g_seccomp && (r == -ENOSYS || ((r == -EPERM || r == -EACCES) && pid == 0));
        if (!blocked || !mask || cpusetsize == 0) return (int) ret(r);
        memset(m, 0xff, cpusetsize);
        return 0;
    }
    size_t got = (size_t) r < cpusetsize ? (size_t) r : cpusetsize;
    if (got<cpusetsize) memset(m+got, 0, cpusetsize-got);
    return 0;
}
static void sys_handler(int sig, void*info, void*uc) {
    (void) sig;
    if (g_trace && info) {
        int nr = *(int*)((char*) info+24);
        char m[48] = "prx: syscall blocked by seccomp: ";
        size_t l = 33;
        char t[12];
        int k = 0;
        unsigned v = nr<0 ? 0 : (unsigned) nr;
        do {
            t[k++] = '0'+v%10;
            v /= 10;
        } while (v);
        while (k) m[l++] = t[--k];
        m[l++] = '\n';
        sc(N_write, 2, (long) m, (long) l, 0, 0, 0);
    }
    *(long*)((char*) uc+UC_RET_OFF) = -ENOSYS;
}
static void install_sigsys(void) {
#if defined(__i386__)
    /* i386 guests run under qemu-user only: no host seccomp SIGSYS to emulate */
    (void) sys_handler;
    return;
#else
    struct ksa a;
    memset(&a, 0, sizeof a);
    a.h = (void*) sys_handler;
    a.flags = 4|0x40000000;
    g_inst = 1;
    sigaction(31, &a, NULL);
    g_inst = 0;
#endif
}
static void seccomp_init(void) {
    if (g_rl && g_seccomp) install_sigsys();
}
/* PR_SET_NO_NEW_PRIVS: really apply it; only when the host refuses it
   (seccomp/SELinux) keep the old behaviour of reporting success. */
static long seccomp_nnp(int(*f)(int, unsigned long, unsigned long, unsigned long, unsigned long), unsigned long a2, unsigned long a3, unsigned long a4, unsigned long a5) {
    if (f) f(38, a2, a3, a4, a5);
    return 0;
}
EXP int sigaction(int sig, const void*act, void*old) {
    static int(*f)(int, const void*, void*);
    if (!f) f = nextsym("sigaction");
    if (sig == 31 && g_rl && g_seccomp && !g_inst) {
        if (old) memset(old, 0, sizeof (struct ksa));
        return 0;
    }
    return f(sig, act, old);
}
EXP void*signal(int sig, void*h) {
    static void*(*f)(int, void*);
    if (!f) f = nextsym("signal");
    if (sig == 31 && g_rl && g_seccomp) return NULL;
    return f(sig, h);
}
EXP void*bsd_signal(int sig, void*h) {
    static void*(*f)(int, void*);
    if (!f) f = nextsym("bsd_signal");
    if (sig == 31 && g_rl && g_seccomp) return NULL;
    return f(sig, h);
}
static const void*unblock_sys(const void*set, int how, u8*tmp) {
    if (!g_rl || !g_seccomp || !set || (how != 0 && how != 2)) return set;
    memcpy(tmp, set, 128);
    tmp[3] &= (u8)~(1u<<6);
    return tmp;
}
EXP int sigprocmask(int how, const void*set, void*old) {
    static int(*f)(int, const void*, void*);
    u8 t[128];
    if (!f) f = nextsym("sigprocmask");
    return f(how, unblock_sys(set, how, t), old);
}
EXP int pthread_sigmask(int how, const void*set, void*old) {
    static int(*f)(int, const void*, void*);
    u8 t[128];
    if (!f) f = nextsym("pthread_sigmask");
    return f(how, unblock_sys(set, how, t), old);
}
#endif /* PRX_NO_SECCOMP */
