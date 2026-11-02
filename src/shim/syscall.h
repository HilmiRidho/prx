/* syscall.h - libc syscall(2): path-translating dispatch for raw callers
 * Part of the prx shim (unity build: included by src/prx.c, not a standalone header).
 *
 * Some programs reach the kernel through libc's variadic syscall() instead of
 * the stat/open wrappers hooked above. libuv (hence Node.js) does this for
 * statx(2), so fs.lstat / fs.stat / require() resolution saw host paths and
 * failed with ENOENT. Path-taking calls are routed to the translating helpers;
 * everything else goes to the real syscall(), which also handles clone() etc. */
static long (*g_real_syscall)(long, long, long, long, long, long, long);
EXP long syscall(long n, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    long a = __builtin_va_arg(ap, long), b = __builtin_va_arg(ap, long);
    long c = __builtin_va_arg(ap, long), d = __builtin_va_arg(ap, long);
    long e = __builtin_va_arg(ap, long), f = __builtin_va_arg(ap, long);
    __builtin_va_end(ap);
    switch (n) {
    case N_statx:
        return statx((int) a, (const char*) b, (int) c, (unsigned) d, (void*) e);
#if !defined(__i386__)
    case N_newfstatat:
        return do_stat((int) a, (const char*) b, (void*) c, (int) d);
#endif
    case N_openat:
        return do_open((int) a, (const char*) b, (int) c, (int) d);
    case N_readlinkat:
        return do_readlink((int) a, (const char*) b, (char*) c, (size_t) d);
    case N_faccessat:
        return do_access((int) a, (const char*) b, (int) c, 0);
#ifdef N_faccessat2
    case N_faccessat2:
        return do_access((int) a, (const char*) b, (int) c, (int) d);
#endif
    }
    if (!g_real_syscall) g_real_syscall = nextsym("syscall");
    if (g_real_syscall) return g_real_syscall(n, a, b, c, d, e, f);
    return ret(sc(n, a, b, c, d, e, f));
}
