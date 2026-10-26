/* prx_core.h - shim state, bind table and start-up
 *
 * The g_* configuration read from the PRX_* environment, the bind table,
 * virtual-credential storage, the SIGSYS handler and prx_do_init(), which
 * runs from the constructor (or earlier, via prx_early()).
 *
 * Part of the shim. Included only by prx.c, in the order listed there:
 * the shim is one translation unit and later parts use what earlier
 * parts define. Do not compile this file on its own.
 */
#ifndef PRX_CORE_H
#define PRX_CORE_H

static char g_root[PM];
static size_t g_rl;
static char g_shim[PM];
#define LPM 65536
static char g_libpath[LPM];
static char g_loader[PM];
static char g_qemu[PM];
/* optional ptrace supervisor for static programs (prx login --ptrace): see prxsup.c */
static char g_sup[PM], g_ld[PM], g_sh[PM];
static char g_traced[8];   /* PRX_TRACED: "1" inside a traced tree, "off" if tracing is unavailable */
static int g_delegate;     /* PRX_DELEGATE=1: this process only exists to run one execve() */
#define BENV 16384
static char g_bindenv[BENV];
static int g_noargv0;
/* comma-separated list of IPv4 and/or IPv6 literals */
static char g_dns[512] = "8.8.8.8,1.1.1.1";
static char g_self_exe[PM];
static int g_fake;
static int g_trace;
static u32 g_ruid, g_rgid;
/* virtual credentials for --no-root-id (host user, setuid/sudo emulation) */
static int g_user;
static char g_vuser[64];
static char g_credbuf[4096];
#define CR_NA ((u32)-1)
enum { C_RU, C_EU, C_SU, C_FU, C_RG, C_EG, C_SG, C_FG };
static u32 g_cr[8];
#define NVG 256
static u32 g_vg[NVG];
static int g_nvg;
static u32 g_pcr[8];
static int g_pcr_ok;
#define VROOT() (g_fake || (g_user && g_cr[C_EU] == 0))
static void cred_init(void);
static int cr_collect(const char*user, u32 gid, u32*out, int max);
#define MAXB 64
#define BPM 1024
static char g_bind[MAXB][BPM];
static size_t g_bindlen[MAXB];
static char g_bhost[MAXB][BPM];
static size_t g_bhlen[MAXB];
static int g_bid[MAXB];
static int g_nb;
static void addbind(const char*s, size_t n) {
    size_t eq = n, i;
    for (i = 0; i<n; i++) {
        if (s[i] == '=') {
            eq = i;
            break;
        }
    }
    size_t gn = eq, hs = eq<n ? eq+1 : 0, hn = eq<n ? n-eq-1 : n;
    const char*hp = eq<n ? s+hs : s;
    if (!gn || g_nb >= MAXB || gn >= sizeof (g_bind[0]) || hn >= sizeof (g_bhost[0]) || s[0] != '/' || hp[0] != '/') return;
    memcpy(g_bind[g_nb], s, gn);
    g_bind[g_nb][gn] = 0;
    while (gn>1 && g_bind[g_nb][gn-1] == '/') g_bind[g_nb][--gn] = 0;
    memcpy(g_bhost[g_nb], hp, hn);
    g_bhost[g_nb][hn] = 0;
    while (hn>1 && g_bhost[g_nb][hn-1] == '/') g_bhost[g_nb][--hn] = 0;
    g_bindlen[g_nb] = gn;
    g_bhlen[g_nb] = hn;
    g_bid[g_nb] = (gn == hn && !memcmp(g_bind[g_nb], g_bhost[g_nb], gn));
    g_nb++;
}
static int envmatch(const char*e, const char*k) {
    size_t n = strlen(k);
    return strncmp(e, k, n) == 0 && e[n] == '=';
}
static void copyval(char*dst, size_t cap, const char*src) {
    size_t n = strlen(src);
    if (n >= cap) n = cap-1;
    memcpy(dst, src, n);
    dst[n] = 0;
}
#if defined(__aarch64__)
#define UC_RET_OFF 184
#elif defined(__i386__)
#define UC_RET_OFF 0
#else
#define UC_RET_OFF 144
#endif
extern int sigaction(int, const void*, void*);
static int g_inst;
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
    (void) pid;
    (void) cpusetsize;
    (void) mask;
    return 0;
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
        int blocked = r == -ENOSYS || ((r == -EPERM || r == -EACCES) && pid == 0);
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
static int g_inited;
static void prx_do_init(char**envp) {
    if (g_inited) return;
    g_inited = 1;
    for (char**e = envp; e && *e; e++) {
        const char*s = *e;
        if (envmatch(s, "PRX_ROOTFS")) copyval(g_root, PM, s+11);
        else if (envmatch(s, "PRX_SHIM")) copyval(g_shim, PM, s+9);
        else if (envmatch(s, "PRX_LOADER")) copyval(g_loader, PM, s+11);
        else if (envmatch(s, "PRX_QEMU")) copyval(g_qemu, PM, s+9);
        else if (envmatch(s, "PRX_LIBPATH")) copyval(g_libpath, LPM, s+12);
        else if (envmatch(s, "PRX_BIND")) copyval(g_bindenv, sizeof g_bindenv, s+9);
        else if (envmatch(s, "PRX_DNS")) copyval(g_dns, sizeof g_dns, s+8);
        else if (envmatch(s, "PRX_SELF_EXE")) copyval(g_self_exe, PM, s+13);
        else if (envmatch(s, "PRX_NO_ARGV0")) g_noargv0 = (s[13] == '1');
        else if (envmatch(s, "PRX_TRACE")) g_trace = (s[10] == '1');
        else if (envmatch(s, "PRX_FAKEROOT")) g_fake = (s[13] == '1');
        else if (envmatch(s, "PRX_VUSER")) copyval(g_vuser, sizeof g_vuser, s+10);
        else if (envmatch(s, "PRX_CRED")) copyval(g_credbuf, sizeof g_credbuf, s+9);
        else if (envmatch(s, "PRX_SUP")) copyval(g_sup, PM, s+8);
        else if (envmatch(s, "PRX_LD")) copyval(g_ld, PM, s+7);
        else if (envmatch(s, "PRX_SH")) copyval(g_sh, PM, s+7);
        else if (envmatch(s, "PRX_TRACED")) copyval(g_traced, sizeof g_traced, s+11);
        else if (envmatch(s, "PRX_DELEGATE")) g_delegate = (s[13] == '1');
    }
    g_rl = strlen(g_root);
    while (g_rl>1 && g_root[g_rl-1] == '/') g_root[--g_rl] = 0;
    if (g_rl == 1) g_rl = 0;
    g_nb = 0;
    addbind("/proc", 5);
    addbind("/sys", 4);
    addbind("/dev/urandom", 12);
    addbind("/dev/random", 11);
    addbind("/dev/zero", 9);
    addbind("/dev/null", 9);
    addbind("/dev/full", 9);
    addbind("/dev/pts", 8);
    addbind("/dev/ptmx", 9);
    addbind("/dev/fd", 7);
    addbind("/dev/stdin", 10);
    addbind("/dev/stdout", 11);
    addbind("/dev/stderr", 11);
    addbind("/dev/tty", 8);
    const char*p = g_bindenv;
    while (*p) {
        const char*q = p;
        while (*q && *q != ':') q++;
        addbind(p, (size_t)(q-p));
        p = *q ? q+1 : q;
    }
    if (g_rl) install_sigsys();
    g_ruid = (u32) sc(N_getuid, 0, 0, 0, 0, 0, 0);
    g_rgid = (u32) sc(N_getgid, 0, 0, 0, 0, 0, 0);
    g_user = (!g_fake && g_vuser[0] && g_rl) ? 1 : 0;
    if (g_user) cred_init();
}
static void prx_delegate(int argc, char**argv);
__attribute__((constructor)) static void prx_init(int argc, char**argv, char**envp) {
    prx_do_init(envp);
    if (g_delegate) prx_delegate(argc, argv);
}
/* LD_PRELOAD constructors run after the constructors of the app's own libs
   (jemalloc, ...). Interposed entry points call this so the SIGSYS handler
   and path translation are active before those constructors do real work. */
static void prx_early(void) {
    if (!g_inited) prx_do_init(environ);
}

#endif /* PRX_CORE_H */
