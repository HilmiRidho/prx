/* pemu_x64 - small x86_64 user-mode emulator, a qemu-user alternative for prx
 * on aarch64 hosts (Termux/Android, Linux ARM). SPDX-License-Identifier: Apache-2.0
 *
 * Guest: x86_64 Linux. Host: aarch64 (the target) or x86_64 (used for testing).
 * The guest is mapped 1:1 into the host address space, so guest pointers are
 * host pointers. Guest syscalls are translated to the host generic syscall
 * ABI (openat, newfstatat, pipe2, ...), so the same code runs on both hosts.
 *
 * Command line (as prx calls qemu-user):
 *     pemu [-0 argv0] [-L prefix] [-E VAR=val] [-U VAR] [-cpu x] [-strace] prog [args...]
 * Environment: QEMU_SET_ENV, QEMU_UNSET_ENV, QEMU_STRACE.
 *
 * Arithmetic flags are evaluated lazily (see mat()/RF()), and jcc/setcc/cmov after cmp/test read the operands directly.
 * CPU model: baseline x86-64 (SSE2) with a small x87 stub. cpuid reports only
 * what is implemented, so glibc selects its SSE2 code paths.
 * Threads (CLONE_THREAD) are supported: each guest thread runs on its own host pthread with its own CPU state.
 * NOT supported: AVX/SSE3+/BMI, most x87, 32-bit code.
 *
 * JIT (aarch64 hosts): guest basic blocks are translated to AArch64 code (see the "JIT" section below). Anything the
 * translator does not know runs through the interpreter from inside the translated block, so behaviour is identical.
 * Disable it with PEMU_JIT=0 or -nojit. The JIT switches itself off when the guest creates a second thread.
 * Not a security boundary. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sched.h>
#include <ucontext.h>
#include <setjmp.h>
#include <math.h>
#include <elf.h>
#include <time.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <pthread.h>
#include <linux/futex.h>
#include <stddef.h>

#if (defined(__aarch64__) || defined(PEMU_JIT_EMBED)) && !defined(PEMU_NO_JIT)
#define HAVE_JIT 1
#endif

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef u16 __attribute__((may_alias, aligned(1))) u16u;
typedef u32 __attribute__((may_alias, aligned(1))) u32u;
typedef u64 __attribute__((may_alias, aligned(1))) u64u;

typedef union { u8 b[16]; s8 sb[16]; u16 w[8]; s16 sw[8]; u32 d[4]; s32 sd[4]; u64 q[2]; s64 sq[2]; float f[4]; double df[2]; } V;

enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI };
#define CF_ 0x1
#define PF_ 0x4
#define AF_ 0x10
#define ZF_ 0x40
#define SF_ 0x80
#define DF_ 0x400
#define OF_ 0x800

typedef struct {
    u64 r[16];
    u64 rip, rf, fs, gs;
    u64 la, lb, lr;                                   /* lazy arithmetic flags: operands/result of the last add/sub/logic op */
    union { struct { u8 lop, lsz, lcin; }; u64 lmeta; };   /* lmeta = lop | lsz << 8 | lcin << 16 (the JIT stores it in one go) */
    u64 patch;                                        /* JIT: chain patch site left by an exit stub */
    V x[16] __attribute__((aligned(16)));
    u32 mxcsr; u16 fcw, fsw;
    long double st[8]; int top;
} CPU;

static CPU cpu;
static __thread CPU *g_cur = &cpu;        /* CPU of the calling host thread (one host thread per guest thread) */
static __thread int g_is_thread;           /* set in threads created by CLONE_THREAD */
static __thread u64 g_clear_tid;           /* CLONE_CHILD_CLEARTID address of this thread */
static int g_trace;
static char g_self[4096];
static u64 g_tramp;
#ifdef HAVE_JIT
static volatile int g_jit_on;                 /* JIT active (cleared when the guest creates a thread) */
static u64 jit_pc2rip(void*uctx);
static void jit_post_syscall(CPU*c, u64 n, long r);
static void jit_note_rwx(u64 start, u64 len);
static void jit_disable(void);
static void jit_report(void);
#endif

/* ------------------------------------------------------------ helpers */
static long hs(long nr, long a, long b, long c_, long d, long e, long f) {
    long r = syscall(nr, a, b, c_, d, e, f);
    return r == -1 ? -errno : r;
}
static inline u64 mskz(int sz) { return sz == 8 ? ~0ULL : (1ULL << (sz * 8)) - 1; }
static inline int sgn(u64 v, int sz) { return (int)((v >> (sz * 8 - 1)) & 1); }
static inline s64 sx(u64 v, int sz) { return sz == 8 ? (s64) v : (s64)(v << (64 - sz * 8)) >> (64 - sz * 8); }
static u64 mrd(u64 a, int n) {
    switch (n) { case 1: return *(u8*) a; case 2: return *(u16u*) a; case 4: return *(u32u*) a; default: return *(u64u*) a; }
}
static void mwr(u64 a, u64 v, int n) {
    switch (n) { case 1: *(u8*) a = (u8) v; break; case 2: *(u16u*) a = (u16) v; break; case 4: *(u32u*) a = (u32) v; break; default: *(u64u*) a = v; }
}

/* ---- lazy flags: add/sub/logic only record their operands; CF/OF/AF/ZF/SF/PF are computed when somebody reads them.
 * RF(c) is an lvalue that first materialises any pending flags, so every other use of the flags stays unchanged. */
enum { LZ_NONE, LZ_ADD, LZ_SUB, LZ_LOGIC };
static const u8 g_par[256] = {
#define PT2(n) n, n ^ 1, n ^ 1, n
#define PT4(n) PT2(n), PT2(n ^ 1), PT2(n ^ 1), PT2(n)
#define PT6(n) PT4(n), PT4(n ^ 1), PT4(n ^ 1), PT4(n)
    PT6(1), PT6(0), PT6(0), PT6(1)
};
#undef PT2
#undef PT4
#undef PT6
static void mat(CPU*c) {
    int sz = c->lsz, sb = sz * 8 - 1; u64 m = mskz(sz), a = c->la, b = c->lb, r = c->lr;
    u64 f = c->rf & ~(u64)(CF_ | OF_ | AF_ | ZF_ | SF_ | PF_);
    switch (c->lop) {
    case LZ_ADD:
        if (c->lcin ? r <= a : r < a) f |= CF_;
        if (((a ^ r) & (b ^ r)) >> sb & 1) f |= OF_;
        if ((a ^ b ^ r) & 0x10) f |= AF_;
        break;
    case LZ_SUB:
        if (c->lcin ? a <= b : a < b) f |= CF_;
        if (((a ^ b) & (a ^ r)) >> sb & 1) f |= OF_;
        if ((a ^ b ^ r) & 0x10) f |= AF_;
        break;
    default: break;
    }
    (void) m;
    if (!r) f |= ZF_;
    if (r >> sb & 1) f |= SF_;
    if (g_par[(u8) r]) f |= PF_;
    c->rf = f; c->lop = LZ_NONE;
}
static inline u64*flagsp(CPU*c) { if (__builtin_expect(c->lop != LZ_NONE, 0)) mat(c); return &c->rf; }
#define RF(c) (*flagsp(c))

static void szp(CPU*c, u64 r, int sz) {
    u64*f = &RF(c);
    *f &= ~(u64)(ZF_ | SF_ | PF_);
    if (!(r & mskz(sz))) *f |= ZF_;
    if (sgn(r, sz)) *f |= SF_;
    if (g_par[(u8) r]) *f |= PF_;
}
static inline u64 f_add(CPU*c, u64 a, u64 b, u64 cin, int sz) {
    u64 m = mskz(sz); a &= m; b &= m;
    u64 r = (a + b + cin) & m;
    c->la = a; c->lb = b; c->lr = r; c->lcin = (u32) cin; c->lsz = sz; c->lop = LZ_ADD;
    return r;
}
static inline u64 f_sub(CPU*c, u64 a, u64 b, u64 bin, int sz) {
    u64 m = mskz(sz); a &= m; b &= m;
    u64 r = (a - b - bin) & m;
    c->la = a; c->lb = b; c->lr = r; c->lcin = (u32) bin; c->lsz = sz; c->lop = LZ_SUB;
    return r;
}
static inline u64 f_logic(CPU*c, u64 r, int sz) {
    r &= mskz(sz);
    c->la = c->lb = 0; c->lr = r; c->lcin = 0; c->lsz = sz; c->lop = LZ_LOGIC;
    return r;
}
/* type: 0 add 1 or 2 adc 3 sbb 4 and 5 sub 6 xor 7 cmp */
static u64 alu(CPU*c, int t, u64 a, u64 b, int sz) {
    switch (t) {
    case 0: return f_add(c, a, b, 0, sz);
    case 1: return f_logic(c, a | b, sz);
    case 2: return f_add(c, a, b, RF(c) & CF_, sz);
    case 3: return f_sub(c, a, b, RF(c) & CF_, sz);
    case 4: return f_logic(c, a & b, sz);
    case 5: case 7: return f_sub(c, a, b, 0, sz);
    default: return f_logic(c, a ^ b, sz);
    }
}
static inline int cond(CPU*c, int cc) {
    int r;
    int lop = c->lop;
    if (lop == LZ_LOGIC || (lop == LZ_SUB && !c->lcin)) {          /* decide directly from the operands, no flag computation */
        int sz = c->lsz, sb = sz * 8 - 1; u64 res = c->lr;
        if (lop == LZ_LOGIC) {
            switch (cc >> 1) {
            case 0: case 1: r = 0; break; case 2: case 3: r = !res; break;
            case 4: case 6: r = (int)(res >> sb & 1); break; case 5: r = g_par[(u8) res]; break;
            default: r = !res || (int)(res >> sb & 1); }
        } else {
            u64 a = c->la, b = c->lb;
            switch (cc >> 1) {
            case 0: r = (int)((((a ^ b) & (a ^ res)) >> sb) & 1); break;
            case 1: r = a < b; break; case 2: r = !res; break; case 3: r = a <= b; break;
            case 4: r = (int)(res >> sb & 1); break; case 5: r = g_par[(u8) res]; break;
            case 6: r = sx(a, sz) < sx(b, sz); break;
            default: r = sx(a, sz) <= sx(b, sz); }
        }
        return (cc & 1) ? !r : r;
    }
    u64 f = RF(c);
    int cf = !!(f & CF_), zf = !!(f & ZF_), sf = !!(f & SF_), of = !!(f & OF_), pf = !!(f & PF_);
    switch (cc >> 1) {
    case 0: r = of; break; case 1: r = cf; break; case 2: r = zf; break; case 3: r = cf || zf; break;
    case 4: r = sf; break; case 5: r = pf; break; case 6: r = sf != of; break; default: r = zf || sf != of; }
    return (cc & 1) ? !r : r;
}
static u64 shiftop(CPU*c, int t, u64 v, unsigned n, int sz) {
    int bits = sz * 8; u64 m = mskz(sz), r = v & m; int cf = 0, of = 0;
    v &= m;
    n &= sz == 8 ? 63 : 31;
    if (!n) return v;
    switch (t) {
    case 4: case 6:
        r = n >= (unsigned) bits ? 0 : (v << n) & m; cf = n > (unsigned) bits ? 0 : (int)((v >> (bits - n)) & 1);
        of = sgn(r, sz) ^ cf; szp(c, r, sz); RF(c) &= ~(u64)AF_; break;
    case 5:
        r = n >= (unsigned) bits ? 0 : v >> n; cf = n > (unsigned) bits ? 0 : (int)((v >> (n - 1)) & 1);
        of = sgn(v, sz); szp(c, r, sz); RF(c) &= ~(u64)AF_; break;
    case 7: {
        s64 sv = sx(v, sz); unsigned k = n >= (unsigned) bits ? (unsigned) bits - 1 : n;
        r = (u64)(sv >> k) & m; cf = (int)((sv >> (k ? k - 1 : 0)) & 1); of = 0; szp(c, r, sz); RF(c) &= ~(u64)AF_; break; }
    case 0: { unsigned k = n % bits; r = k ? ((v << k) | (v >> (bits - k))) & m : v; cf = (int)(r & 1); of = sgn(r, sz) ^ cf; break; }
    case 1: { unsigned k = n % bits; r = k ? ((v >> k) | (v << (bits - k))) & m : v; cf = sgn(r, sz); of = cf ^ (int)((r >> (bits - 2)) & 1); break; }
    case 2: case 3: {
        int cc = !!(RF(c) & CF_); r = v;
        for (unsigned i = 0; i < n % (bits + 1); i++) {
            if (t == 2) { int o = sgn(r, sz); r = ((r << 1) | (u64) cc) & m; cc = o; }
            else { int o = (int)(r & 1); r = (r >> 1) | ((u64) cc << (bits - 1)); cc = o; }
        }
        cf = cc; of = t == 2 ? sgn(r, sz) ^ cf : sgn(r, sz) ^ (int)((r >> (bits - 2)) & 1); break; }
    }
    RF(c) &= ~(u64)(CF_ | OF_);
    if (cf) RF(c) |= CF_;
    if (of) RF(c) |= OF_;
    return r;
}

/* ------------------------------------------------------------ signals */
struct gsa { u64 handler, flags, restorer, mask; };
static struct gsa gsa[65];
static volatile u64 pend;
static siginfo_t pinfo[65];
static __thread sigjmp_buf g_jb;
static __thread volatile int g_fault; static __thread volatile u64 g_faddr;

/* Android (and containers) kill a process with SIGSYS when it calls a syscall its seccomp filter does not allow.
 * The emulator forwards guest syscalls to the host, so turn such a kill into -ENOSYS: guests (glibc) then fall back. */
static void hsys(int s, siginfo_t*si, void*u) {
    ucontext_t*uc = u; (void) s;
#ifdef si_syscall
    if (g_trace && si) fprintf(stderr, "pemu: host seccomp blocked syscall %d -> ENOSYS\n", (int) si->si_syscall);
#else
    (void) si;
    if (g_trace) fprintf(stderr, "pemu: host seccomp blocked a syscall -> ENOSYS\n");
#endif
#if defined(__aarch64__)
    uc->uc_mcontext.regs[0] = (u64)(s64) -ENOSYS;
#elif defined(__x86_64__)
    uc->uc_mcontext.gregs[REG_RAX] = -ENOSYS;
#else
    (void) uc;
#endif
}
static void hsig(int s, siginfo_t*si, void*u) { (void) u; if (si) pinfo[s] = *si; __atomic_or_fetch(&pend, 1ULL << s, __ATOMIC_SEQ_CST); }
static void hsegv(int s, siginfo_t*si, void*u) {
#ifdef HAVE_JIT
    if (g_jit_on) { u64 r = jit_pc2rip(u); if (r) g_cur->rip = r; }       /* fault inside translated code: recover the guest rip */
#else
    (void) u;
#endif
    if (gsa[s].handler > 1) { g_fault = s; g_faddr = (u64) si->si_addr; pinfo[s] = *si; siglongjmp(g_jb, 1); }
    fprintf(stderr, "pemu: guest fault (signal %d) at addr=%p rip=%#llx\n", s, si->si_addr, (unsigned long long) g_cur->rip);
    signal(s, SIG_DFL); raise(s);
    _exit(128 + s);
}
static const int gidx[16] = {8, 9, 10, 11, 12, 13, 14, 15, 7, 6, 5, 3, 2, 0, 1, 4};
#define SFRAME 1088
static void deliver_sig(CPU*c, int s) {
    struct gsa*h = &gsa[s];
    u64 sp = ((c->r[RSP] - 128 - SFRAME) & ~15ULL) - 8;
    memset((void*) sp, 0, SFRAME);
    u64 uc = sp + 8, info = sp + 8 + 424, fp = sp + 568;
    memcpy((void*) info, &pinfo[s], 128);
    u64 old = 0;
    syscall(SYS_rt_sigprocmask, SIG_BLOCK, NULL, &old, 8);
    *(u64*)(uc + 296) = old;
    u64 mc = uc + 40;
    for (int i = 0; i < 16; i++) *(u64*)(mc + 8 * (u64) i) = c->r[gidx[i]];
    *(u64*)(mc + 8 * 16) = c->rip; *(u64*)(mc + 8 * 17) = RF(c);
    *(u64*)(mc + 8 * 22) = g_fault == s ? g_faddr : 0;
    *(u64*)(mc + 8 * 23) = fp;
    memcpy((void*)(fp + 160), c->x, 256); *(u32*)(fp + 24) = c->mxcsr;
    u64 nm = old | h->mask | ((h->flags & 0x40000000) ? 0 : (1ULL << (s - 1)));
    syscall(SYS_rt_sigprocmask, SIG_SETMASK, &nm, NULL, 8);
    *(u64*) sp = (h->flags & 0x04000000) ? h->restorer : g_tramp;
    c->r[RSP] = sp; c->r[RDI] = (u64) s; c->r[RSI] = info; c->r[RDX] = uc; c->r[RAX] = 0;
    c->rip = h->handler; RF(c) &= ~(u64)DF_;
    if (h->flags & 0x80000000u) h->handler = 0;
    g_fault = 0;
}
static void g_sigreturn(CPU*c) {
    u64 sp = c->r[RSP] - 8, uc = sp + 8, mc = uc + 40, fp = *(u64*)(mc + 8 * 23);
    for (int i = 0; i < 16; i++) c->r[gidx[i]] = *(u64*)(mc + 8 * (u64) i);
    c->rip = *(u64*)(mc + 8 * 16); RF(c) = *(u64*)(mc + 8 * 17);
    if (fp) { memcpy(c->x, (void*)(fp + 160), 256); c->mxcsr = *(u32*)(fp + 24); }
    u64 m = *(u64*)(uc + 296);
    syscall(SYS_rt_sigprocmask, SIG_SETMASK, &m, NULL, 8);
}
static void check_pending(CPU*c) {
    while (pend) {
        u64 p = pend; int s = __builtin_ctzll(p);
        __atomic_and_fetch(&pend, ~(1ULL << s), __ATOMIC_SEQ_CST);
        if (gsa[s].handler > 1) deliver_sig(c, s);
    }
}

/* ------------------------------------------------------------ syscalls */
static u64 brk_base, brk_cur, brk_end;

struct fl { long g, h; };
static const struct fl oflags[] = {
    {0100, O_CREAT}, {0200, O_EXCL}, {0400, O_NOCTTY}, {01000, O_TRUNC}, {02000, O_APPEND}, {04000, O_NONBLOCK},
    {010000, O_DSYNC}, {020000, O_ASYNC}, {040000, O_DIRECT}, {0200000, O_DIRECTORY}, {0400000, O_NOFOLLOW},
    {01000000, O_NOATIME}, {02000000, O_CLOEXEC}, {04000000, O_SYNC & ~O_DSYNC}, {010000000, O_PATH},
};
static long fl_g2h(long f) {
    long r = f & 3;
    for (unsigned k = 0; k < sizeof oflags / sizeof *oflags; k++) if ((f & oflags[k].g) == oflags[k].g) r |= oflags[k].h;
    if ((f & 020200000) == 020200000) r |= O_TMPFILE;
    return r;
}
static long fl_h2g(long f) {
    long r = f & 3;
    for (unsigned k = 0; k < sizeof oflags / sizeof *oflags; k++) if ((f & oflags[k].h) == oflags[k].h && oflags[k].h) r |= oflags[k].g;
    return r;
}
static void cv_stat(const struct stat*s, u8*g) {
    memset(g, 0, 144);
    *(u64*)(g + 0) = s->st_dev; *(u64*)(g + 8) = s->st_ino; *(u64*)(g + 16) = s->st_nlink;
    *(u32*)(g + 24) = s->st_mode; *(u32*)(g + 28) = s->st_uid; *(u32*)(g + 32) = s->st_gid;
    *(u64*)(g + 40) = s->st_rdev; *(s64*)(g + 48) = s->st_size; *(s64*)(g + 56) = s->st_blksize; *(s64*)(g + 64) = s->st_blocks;
    *(s64*)(g + 72) = s->st_atim.tv_sec; *(s64*)(g + 80) = s->st_atim.tv_nsec;
    *(s64*)(g + 88) = s->st_mtim.tv_sec; *(s64*)(g + 96) = s->st_mtim.tv_nsec;
    *(s64*)(g + 104) = s->st_ctim.tv_sec; *(s64*)(g + 112) = s->st_ctim.tv_nsec;
}
static long g_stat(long dirfd, const char*p, u8*out, long flags) {
    struct stat st;
    long r = hs(SYS_newfstatat, dirfd, (long) p, (long) &st, flags, 0, 0);
    if (r == 0) cv_stat(&st, out);
    return r;
}
static int is_x64_elf(const char*p) {
    int fd = open(p, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    unsigned char h[20];
    ssize_t n = read(fd, h, 20);
    close(fd);
    return n == 20 && !memcmp(h, "\177ELF", 4) && h[4] == 2 && *(u16*)(h + 18) == EM_X86_64;
}
static long do_execve(const char*path, char**argv, char**envp) {
    if (!is_x64_elf(path)) return hs(SYS_execve, (long) path, (long) argv, (long) envp, 0, 0, 0);
    int n = 0; while (argv && argv[n]) n++;
    char**nv = calloc((size_t) n + 6, sizeof *nv);
    int k = 0;
    nv[k++] = g_self; nv[k++] = "-0"; nv[k++] = n ? argv[0] : (char*) path; nv[k++] = (char*) path;
    for (int i = 1; i < n; i++) nv[k++] = argv[i];
    long r = hs(SYS_execve, (long) g_self, (long) nv, (long) envp, 0, 0, 0);
    free(nv);
    return r;
}

/* direct pass-through: guest x86_64 number -> host number (same arguments) */
/* less common pass-through syscalls; each guarded because headers differ per host */
#define P2(g, name) case g: return name;
static long pass_nr2(u64 n) {
    switch (n) {
#ifdef SYS_setxattr
    P2(188, SYS_setxattr) P2(189, SYS_lsetxattr) P2(190, SYS_fsetxattr) P2(191, SYS_getxattr) P2(192, SYS_lgetxattr)
    P2(193, SYS_fgetxattr) P2(194, SYS_listxattr) P2(195, SYS_llistxattr) P2(196, SYS_flistxattr)
    P2(197, SYS_removexattr) P2(198, SYS_lremovexattr) P2(199, SYS_fremovexattr)
#endif
    P2(140, SYS_getpriority) P2(141, SYS_setpriority) P2(142, SYS_sched_setparam) P2(143, SYS_sched_getparam)
    P2(144, SYS_sched_setscheduler) P2(145, SYS_sched_getscheduler) P2(146, SYS_sched_get_priority_max)
    P2(147, SYS_sched_get_priority_min) P2(148, SYS_sched_rr_get_interval) P2(149, SYS_mlock) P2(150, SYS_munlock)
    P2(151, SYS_mlockall) P2(152, SYS_munlockall) P2(135, SYS_personality) P2(165, SYS_mount) P2(166, SYS_umount2)
    P2(170, SYS_sethostname) P2(227, SYS_clock_settime) P2(254, SYS_inotify_add_watch) P2(255, SYS_inotify_rm_watch)
    P2(275, SYS_splice) P2(276, SYS_tee) P2(277, SYS_sync_file_range) P2(278, SYS_vmsplice) P2(283, SYS_timerfd_create)
    P2(286, SYS_timerfd_settime) P2(287, SYS_timerfd_gettime) P2(289, SYS_signalfd4) P2(309, SYS_getcpu)
    P2(222, SYS_timer_create) P2(223, SYS_timer_settime) P2(224, SYS_timer_gettime) P2(225, SYS_timer_getoverrun)
    P2(226, SYS_timer_delete) P2(131 + 1000, SYS_sync)
#ifdef SYS_close_range
    P2(436, SYS_close_range)
#endif
#ifdef SYS_pidfd_open
    P2(434, SYS_pidfd_open) P2(424, SYS_pidfd_send_signal)
#endif
    default: return -1;
    }
}

/* direct pass-through: see pass_nr2 for the rest */
static long pass_nr(u64 n) {
    switch (n) {
    case 0: return SYS_read; case 1: return SYS_write; case 3: return SYS_close; case 8: return SYS_lseek;
    case 9: return SYS_mmap; case 10: return SYS_mprotect; case 11: return SYS_munmap; case 16: return SYS_ioctl;
    case 17: return SYS_pread64; case 18: return SYS_pwrite64; case 19: return SYS_readv; case 20: return SYS_writev;
    case 24: return SYS_sched_yield; case 25: return SYS_mremap; case 26: return SYS_msync; case 27: return SYS_mincore;
    case 28: return SYS_madvise; case 32: return SYS_dup; case 35: return SYS_nanosleep; case 36: return SYS_getitimer;
    case 38: return SYS_setitimer; case 39: return SYS_getpid; case 40: return SYS_sendfile; case 41: return SYS_socket;
    case 42: return SYS_connect; case 43: return SYS_accept; case 44: return SYS_sendto; case 45: return SYS_recvfrom;
    case 46: return SYS_sendmsg; case 47: return SYS_recvmsg; case 48: return SYS_shutdown; case 49: return SYS_bind;
    case 50: return SYS_listen; case 51: return SYS_getsockname; case 52: return SYS_getpeername;
    case 53: return SYS_socketpair; case 54: return SYS_setsockopt; case 55: return SYS_getsockopt;
    case 61: return SYS_wait4; case 62: return SYS_kill; case 73: return SYS_flock; case 74: return SYS_fsync;
    case 75: return SYS_fdatasync; case 76: return SYS_truncate; case 77: return SYS_ftruncate; case 79: return SYS_getcwd;
    case 80: return SYS_chdir; case 81: return SYS_fchdir; case 91: return SYS_fchmod; case 93: return SYS_fchown;
    case 95: return SYS_umask; case 96: return SYS_gettimeofday; case 97: return SYS_getrlimit; case 98: return SYS_getrusage;
    case 99: return SYS_sysinfo; case 100: return SYS_times; case 102: return SYS_getuid; case 104: return SYS_getgid;
    case 105: return SYS_setuid; case 106: return SYS_setgid; case 107: return SYS_geteuid; case 108: return SYS_getegid;
    case 109: return SYS_setpgid; case 110: return SYS_getppid; case 112: return SYS_setsid; case 113: return SYS_setreuid;
    case 114: return SYS_setregid; case 115: return SYS_getgroups; case 116: return SYS_setgroups;
    case 117: return SYS_setresuid; case 118: return SYS_getresuid; case 119: return SYS_setresgid;
    case 120: return SYS_getresgid; case 121: return SYS_getpgid; case 124: return SYS_getsid; case 125: return SYS_capget;
    case 126: return SYS_capset; case 127: return SYS_rt_sigpending; case 128: return SYS_rt_sigtimedwait;
    case 130: return SYS_rt_sigsuspend; case 137: return SYS_statfs; case 138: return SYS_fstatfs; case 157: return SYS_prctl;
    case 160: return SYS_setrlimit; case 161: return SYS_chroot; case 162: return SYS_sync; case 186: return SYS_gettid;
    case 200: return SYS_tkill; case 202: return SYS_futex; case 203: return SYS_sched_setaffinity;
    case 204: return SYS_sched_getaffinity; case 217: return SYS_getdents64; case 218: return SYS_set_tid_address;
    case 221: return SYS_fadvise64; case 228: return SYS_clock_gettime; case 229: return SYS_clock_getres;
    case 230: return SYS_clock_nanosleep; case 233: return SYS_epoll_ctl; case 234: return SYS_tgkill;
    case 258: return SYS_mkdirat; case 259: return SYS_mknodat; case 260: return SYS_fchownat; case 263: return SYS_unlinkat;
    case 264: return SYS_renameat; case 265: return SYS_linkat; case 266: return SYS_symlinkat;
    case 267: return SYS_readlinkat; case 268: return SYS_fchmodat; case 269: return SYS_faccessat;
    case 270: return SYS_pselect6; case 271: return SYS_ppoll; case 273: return SYS_set_robust_list;
    case 274: return SYS_get_robust_list; case 280: return SYS_utimensat; case 285: return SYS_fallocate;
    case 288: return SYS_accept4; case 290: return SYS_eventfd2; case 291: return SYS_epoll_create1;
    case 293: return SYS_pipe2; case 294: return SYS_inotify_init1; case 302: return SYS_prlimit64;
    case 316: return SYS_renameat2; case 318: return SYS_getrandom; case 319: return SYS_memfd_create;
    case 332: return SYS_statx; case 247: return SYS_waitid; case 306: return SYS_syncfs; case 326: return SYS_copy_file_range;
    case 295: return SYS_preadv; case 296: return SYS_pwritev; case 299: return SYS_recvmmsg; case 307: return SYS_sendmmsg;
    case 5: return -2;
    default: return pass_nr2(n);
    }
}

/* ---- threads: every guest thread is one host pthread running its own interpreter loop ---- */
static void run(CPU*c);
typedef struct { CPU cpu; u64 flags, ptid, ctid; volatile int*tidout; } ThArg;
static void thread_exit(CPU*c) {
    if (g_clear_tid) {
        __atomic_store_n((u32*) g_clear_tid, 0, __ATOMIC_SEQ_CST);
        syscall(SYS_futex, g_clear_tid, FUTEX_WAKE, 1, NULL, NULL, 0);
    }
    free(c);                                              /* c is the ThArg (cpu is its first member) */
    pthread_exit(NULL);
}
static void*thread_main(void*p) {
    ThArg*t = p; CPU*c = &t->cpu;
    g_cur = c; g_is_thread = 1;
    int tid = (int) syscall(SYS_gettid);
    if (t->flags & 0x00100000) *(u32*) t->ptid = (u32) tid;            /* PARENT_SETTID, before the guest runs */
    if (t->flags & 0x01000000) *(u32*) t->ctid = (u32) tid;            /* CHILD_SETTID */
    if (t->flags & 0x00200000) g_clear_tid = t->ctid;                  /* CHILD_CLEARTID */
    __atomic_store_n(t->tidout, tid, __ATOMIC_SEQ_CST);                 /* t may be freed once the thread exits: do not touch it after run() */
    run(c);
    return NULL;
}
static long clone_thread(CPU*c, u64 flags, u64 stack, u64 ptid, u64 ctid, u64 tls) {
    if ((flags & 0x100) == 0 || (flags & 0x800) == 0) return -EINVAL;   /* threads need CLONE_VM | CLONE_SIGHAND */
    ThArg*t = malloc(sizeof *t);
    if (!t) return -ENOMEM;
#ifdef HAVE_JIT
    if (g_jit_on) jit_disable();                          /* the code cache is not shared safely between threads: go back to the interpreter */
#endif
    t->cpu = *c; t->flags = flags; t->ptid = ptid; t->ctid = ctid; volatile int tidv = 0; t->tidout = &tidv;
    if (stack) t->cpu.r[RSP] = stack;
    if (flags & 0x00080000) t->cpu.fs = tls;                            /* SETTLS */
    t->cpu.r[RAX] = 0;
    pthread_attr_t at; pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&at, 1UL << 20);
    pthread_t th; int e = pthread_create(&th, &at, thread_main, t);
    pthread_attr_destroy(&at);
    if (e) { free(t); return -e; }
    int tid;
    while (!(tid = __atomic_load_n(&tidv, __ATOMIC_SEQ_CST))) sched_yield();
    return tid;
}

static long gsys(CPU*c, int*noset) {
    u64 n = c->r[RAX], a0 = c->r[RDI], a1 = c->r[RSI], a2 = c->r[RDX], a3 = c->r[10], a4 = c->r[8], a5 = c->r[9];
    long r, nr;
    switch (n) {
#ifdef HAVE_JIT
    case 231: if (g_jit_on) jit_report(); return hs(SYS_exit_group, (long) a0, 0, 0, 0, 0, 0);
#else
    case 231: return hs(SYS_exit_group, (long) a0, 0, 0, 0, 0, 0);
#endif
    case 60:
        if (g_is_thread) thread_exit(c);                /* does not return */
#ifdef HAVE_JIT
        if (g_jit_on) jit_report();
#endif
        return hs(SYS_exit_group, (long) a0, 0, 0, 0, 0, 0);
    case 2: return hs(SYS_openat, AT_FDCWD, (long) a0, fl_g2h((long) a1), (long) a2, 0, 0);
    case 85: return hs(SYS_openat, AT_FDCWD, (long) a0, O_CREAT | O_WRONLY | O_TRUNC, (long) a1, 0, 0);
    case 257: return hs(SYS_openat, (long) a0, (long) a1, fl_g2h((long) a2), (long) a3, 0, 0);
    case 4: return g_stat(AT_FDCWD, (const char*) a0, (u8*) a1, 0);
    case 6: return g_stat(AT_FDCWD, (const char*) a0, (u8*) a1, AT_SYMLINK_NOFOLLOW);
    case 262: return g_stat((long) a0, (const char*) a1, (u8*) a2, (long) a3);
    case 5: { struct stat st; r = hs(SYS_fstat, (long) a0, (long) &st, 0, 0, 0, 0); if (r == 0) cv_stat(&st, (u8*) a1); return r; }
    case 7: { struct timespec ts, *tp = NULL; if ((int) a2 >= 0) { ts.tv_sec = (int) a2 / 1000; ts.tv_nsec = ((int) a2 % 1000) * 1000000L; tp = &ts; }
              return hs(SYS_ppoll, (long) a0, (long) a1, (long) tp, 0, 8, 0); }
    case 21: return hs(SYS_faccessat, AT_FDCWD, (long) a0, (long) a1, 0, 0, 0);
    case 22: return hs(SYS_pipe2, (long) a0, 0, 0, 0, 0, 0);
    case 23: { struct timespec ts, *tp = NULL; if (a4) { struct timeval*tv = (struct timeval*) a4; ts.tv_sec = tv->tv_sec; ts.tv_nsec = tv->tv_usec * 1000L; tp = &ts; }
               return hs(SYS_pselect6, (long) a0, (long) a1, (long) a2, (long) a3, (long) tp, 0); }
    case 33: if (a0 == a1) return hs(SYS_fcntl, (long) a0, F_GETFD, 0, 0, 0, 0) < 0 ? -EBADF : (long) a1;
             return hs(SYS_dup3, (long) a0, (long) a1, 0, 0, 0, 0);
    case 292: return hs(SYS_dup3, (long) a0, (long) a1, fl_g2h((long) a2) & O_CLOEXEC, 0, 0, 0);
    case 34: return hs(SYS_ppoll, 0, 0, 0, 0, 8, 0);
    case 37: { struct itimerval it = {{0, 0}, {(time_t) a0, 0}}; return hs(SYS_setitimer, ITIMER_REAL, (long) &it, 0, 0, 0, 0); }
    case 82: return hs(SYS_renameat, AT_FDCWD, (long) a0, AT_FDCWD, (long) a1, 0, 0);
    case 83: return hs(SYS_mkdirat, AT_FDCWD, (long) a0, (long) a1, 0, 0, 0);
    case 84: return hs(SYS_unlinkat, AT_FDCWD, (long) a0, AT_REMOVEDIR, 0, 0, 0);
    case 86: return hs(SYS_linkat, AT_FDCWD, (long) a0, AT_FDCWD, (long) a1, 0, 0);
    case 87: return hs(SYS_unlinkat, AT_FDCWD, (long) a0, 0, 0, 0, 0);
    case 88: return hs(SYS_symlinkat, (long) a0, AT_FDCWD, (long) a1, 0, 0, 0);
    case 89: return hs(SYS_readlinkat, AT_FDCWD, (long) a0, (long) a1, (long) a2, 0, 0);
    case 90: return hs(SYS_fchmodat, AT_FDCWD, (long) a0, (long) a1, 0, 0, 0);
    case 92: return hs(SYS_fchownat, AT_FDCWD, (long) a0, (long) a1, (long) a2, 0, 0);
    case 94: return hs(SYS_fchownat, AT_FDCWD, (long) a0, (long) a1, (long) a2, AT_SYMLINK_NOFOLLOW, 0);
    case 235: { struct timespec ts[2], *tp = NULL; if (a1) { struct timeval*tv = (struct timeval*) a1; for (int i = 0; i < 2; i++) { ts[i].tv_sec = tv[i].tv_sec; ts[i].tv_nsec = tv[i].tv_usec * 1000L; } tp = ts; }
                return hs(SYS_utimensat, AT_FDCWD, (long) a0, (long) tp, 0, 0, 0); }
    case 201: { struct timespec ts; r = hs(SYS_clock_gettime, CLOCK_REALTIME, (long) &ts, 0, 0, 0, 0); if (r == 0 && a0) *(s64*) a0 = ts.tv_sec; return r == 0 ? (long) ts.tv_sec : r; }
    case 72:
        if (a1 == F_GETFL) { r = hs(SYS_fcntl, (long) a0, F_GETFL, 0, 0, 0, 0); return r < 0 ? r : fl_h2g(r); }
        if (a1 == F_SETFL) return hs(SYS_fcntl, (long) a0, F_SETFL, fl_g2h((long) a2), 0, 0, 0);
        return hs(SYS_fcntl, (long) a0, (long) a1, (long) a2, 0, 0, 0);
    case 63: { struct utsname u; r = hs(SYS_uname, (long) &u, 0, 0, 0, 0, 0); if (r == 0) { strcpy(u.machine, "x86_64"); memcpy((void*) a0, &u, sizeof u); } return r; }
    case 12:
        if (a0 >= brk_base && a0 <= brk_end) brk_cur = a0;
        return (long) brk_cur;
    case 131: return 0;
    case 111: return hs(SYS_getpgid, 0, 0, 0, 0, 0, 0);
    case 158:                                         /* arch_prctl */
        switch (a0) {
        case 0x1002: c->fs = a1; return 0;
        case 0x1001: c->gs = a1; return 0;
        case 0x1003: *(u64*) a1 = c->fs; return 0;
        case 0x1004: *(u64*) a1 = c->gs; return 0;
        default: return -EINVAL;
        }
    case 13: {                                        /* rt_sigaction */
        int s = (int) a0;
        if (s < 1 || s > 64 || s == SIGKILL || s == SIGSTOP) return -EINVAL;
        struct gsa old = gsa[s];
        if (a1) {
            struct gsa nw = *(struct gsa*) a1;
            struct sigaction sa; memset(&sa, 0, sizeof sa);
            sigfillset(&sa.sa_mask);
            if (nw.handler == 0) sa.sa_handler = SIG_DFL;
            else if (nw.handler == 1) sa.sa_handler = SIG_IGN;
            else { sa.sa_sigaction = hsig; sa.sa_flags = SA_SIGINFO | (int)(nw.flags & SA_RESTART); }
            if (s == SIGSEGV || s == SIGBUS || s == SIGILL || s == SIGFPE) { if (nw.handler > 1) sa.sa_sigaction = hsegv; sa.sa_flags |= SA_NODEFER; }
            if (s != SIGSYS && sigaction(s, &sa, NULL) < 0) return -errno;     /* SIGSYS stays with hsys() */
            gsa[s] = nw;
        }
        if (a2) *(struct gsa*) a2 = old;
        return 0;
    }
    case 14: {                                        /* rt_sigprocmask: never let the guest block SIGSYS on the host, or a seccomp kill cannot be turned into ENOSYS (see hsys) */
        long set = (long) a1; u64 nm;
        if (a1 && (int) a0 != 1) { nm = *(u64*) a1 & ~(1ULL << (SIGSYS - 1)); set = (long) &nm; }
        return hs(SYS_rt_sigprocmask, (long) a0, set, (long) a2, 8, 0, 0); }
    case 15: g_sigreturn(c); *noset = 1; return 0;
    case 232: case 281: {                             /* epoll_wait / epoll_pwait: guest events are packed (12 bytes) */
        int max = (int) a2; if (max <= 0 || max > 4096) return -EINVAL;
        struct epoll_event*ev = malloc(sizeof *ev * (size_t) max);
        r = hs(SYS_epoll_pwait, (long) a0, (long) ev, max, (long) a3, n == 281 ? (long) a4 : 0, 8);
        for (long i = 0; i < r; i++) { u8*g = (u8*) a1 + 12 * i; *(u32*) g = ev[i].events; *(u64u*)(g + 4) = ev[i].data.u64; }
        free(ev); return r;
    }
    case 213: return hs(SYS_epoll_create1, 0, 0, 0, 0, 0, 0);
    case 284: return hs(SYS_eventfd2, (long) a0, 0, 0, 0, 0, 0);
    case 22 + 1000: return 0;
    case 56: case 57: case 58: {                      /* clone / fork / vfork */
        u64 flags = n == 56 ? a0 : SIGCHLD;
        if (flags & 0x10000 /*CLONE_THREAD*/) return clone_thread(c, flags, a1, a2, a3, a4);
        long pid = fork();
        if (pid < 0) return -errno;
        if (pid == 0) {
            if (n == 56 && a1) c->r[RSP] = a1;
            if (n == 56 && (flags & 0x01000000)) *(u32*) a3 = (u32) getpid();     /* CHILD_SETTID: ctid */
            if (n == 56 && (flags & 0x00080000)) c->fs = a4;                        /* SETTLS */
            c->r[RAX] = 0; *noset = 1;
        } else if (n == 56 && (flags & 0x00100000)) *(u32*) a2 = (u32) pid;      /* PARENT_SETTID */
        return pid;
    }
    case 59: return do_execve((const char*) a0, (char**) a1, (char**) a2);
    case 322: case 435: case 334: case 317: return -ENOSYS;
    case 273: case 274: return -ENOSYS;               /* set/get_robust_list: blocked by Android seccomp; glibc ignores the error */
    default:
        nr = pass_nr(n);
        if (nr >= 0) return hs(nr, (long) a0, (long) a1, (long) a2, (long) a3, (long) a4, (long) a5);
        if (g_trace) fprintf(stderr, "pemu: unsupported syscall %llu\n", (unsigned long long) n);
        return -ENOSYS;
    }
}
static void do_syscall(CPU*c) {
    int noset = 0;
    u64 n = c->r[RAX];
    if (g_trace) fprintf(stderr, "pemu: syscall %llu ...\n", (unsigned long long) n);
    long r = gsys(c, &noset);
    if (g_trace) fprintf(stderr, "pemu: syscall %llu -> %ld\n", (unsigned long long) n, r);
    if (!noset) c->r[RAX] = (u64) r;
#ifdef HAVE_JIT
    if (g_jit_on) jit_post_syscall(c, n, r);
#endif
}

/* ------------------------------------------------------------ decoder */
typedef struct { u8*ip; int rex, w, p66, rep, repne, addr32; u64 seg; } I;
typedef struct { int mod, reg, rm, ismem; u64 ea; } M;

static u64 getr(CPU*c, int n, int sz, int rex) {
    switch (sz) {
    case 8: return c->r[n];
    case 4: return (u32) c->r[n];
    case 2: return (u16) c->r[n];
    default: if (!rex && n >= 4 && n < 8) return (c->r[n - 4] >> 8) & 0xff; return c->r[n] & 0xff;
    }
}
static void setr(CPU*c, int n, int sz, int rex, u64 v) {
    switch (sz) {
    case 8: c->r[n] = v; break;
    case 4: c->r[n] = (u32) v; break;
    case 2: c->r[n] = (c->r[n] & ~0xffffULL) | (u16) v; break;
    default:
        if (!rex && n >= 4 && n < 8) c->r[n - 4] = (c->r[n - 4] & ~0xff00ULL) | ((v & 0xff) << 8);
        else c->r[n] = (c->r[n] & ~0xffULL) | (v & 0xff);
    }
}
static void modrm(CPU*c, I*in, M*m, int immbytes) {
    u8 b = *in->ip++;
    m->mod = b >> 6; m->reg = ((b >> 3) & 7) | ((in->rex & 4) << 1); m->rm = (b & 7) | ((in->rex & 1) << 3);
    if (m->mod == 3) { m->ismem = 0; return; }
    m->ismem = 1;
    u64 ea = 0;
    if ((b & 7) == 4) {
        u8 sib = *in->ip++;
        int sc = sib >> 6, idx = ((sib >> 3) & 7) | ((in->rex & 2) << 2), base = (sib & 7) | ((in->rex & 1) << 3);
        if (idx != 4) ea += c->r[idx] << sc;
        if ((sib & 7) == 5 && m->mod == 0) { ea += (u64)(s64)(s32) *(u32u*) in->ip; in->ip += 4; }
        else ea += c->r[base];
    } else if ((b & 7) == 5 && m->mod == 0) {
        s32 d = (s32) *(u32u*) in->ip; in->ip += 4;
        ea = (u64)(in->ip + immbytes) + (u64)(s64) d;
    } else ea = c->r[m->rm];
    if (m->mod == 1) { ea += (u64)(s64)(s8) *in->ip; in->ip += 1; }
    else if (m->mod == 2) { ea += (u64)(s64)(s32) *(u32u*) in->ip; in->ip += 4; }
    if (in->addr32) ea &= 0xffffffffULL;
    m->ea = ea + in->seg;
}
static inline u64 rdm(CPU*c, I*in, M*m, int sz) { return m->ismem ? mrd(m->ea, sz) : getr(c, m->rm, sz, in->rex); }
static inline void wrm(CPU*c, I*in, M*m, int sz, u64 v) { if (m->ismem) mwr(m->ea, v, sz); else setr(c, m->rm, sz, in->rex, v); }
static inline s32 imm32(I*in) { s32 v = (s32) *(u32u*) in->ip; in->ip += 4; return v; }
static inline u64 immz(I*in, int sz) {                       /* Iz sign-extended to 64 */
    if (sz == 2) { u64 v = (u64)(s64)(s16) *(u16u*) in->ip; in->ip += 2; return v; }
    return (u64)(s64) imm32(in);
}
static void push(CPU*c, u64 v) { c->r[RSP] -= 8; mwr(c->r[RSP], v, 8); }
static u64 pop(CPU*c) { u64 v = mrd(c->r[RSP], 8); c->r[RSP] += 8; return v; }

static void undef(CPU*c, const u8*ip, const char*why) {
    fprintf(stderr, "pemu: unsupported instruction (%s) at rip=%#llx: %02x %02x %02x %02x %02x %02x\n", why, (unsigned long long) c->rip,
            ip[0], ip[1], ip[2], ip[3], ip[4], ip[5]);
    signal(SIGILL, SIG_DFL); raise(SIGILL); _exit(132);
}

/* ------------------------------------------------------------ SSE2 */
static void pint(int op, V*d, const V*s, int*ok) {
    V t = *d; int i;
    switch (op) {
    case 0x60: for (i = 0; i < 8; i++) { d->b[2 * i] = t.b[i]; d->b[2 * i + 1] = s->b[i]; } break;
    case 0x61: for (i = 0; i < 4; i++) { d->w[2 * i] = t.w[i]; d->w[2 * i + 1] = s->w[i]; } break;
    case 0x62: for (i = 0; i < 2; i++) { d->d[2 * i] = t.d[i]; d->d[2 * i + 1] = s->d[i]; } break;
    case 0x63: for (i = 0; i < 8; i++) { int v = t.sw[i]; d->sb[i] = (s8)(v > 127 ? 127 : v < -128 ? -128 : v); int w = s->sw[i]; d->sb[8 + i] = (s8)(w > 127 ? 127 : w < -128 ? -128 : w); } break;
    case 0x64: for (i = 0; i < 16; i++) d->b[i] = t.sb[i] > s->sb[i] ? 0xff : 0; break;
    case 0x65: for (i = 0; i < 8; i++) d->w[i] = t.sw[i] > s->sw[i] ? 0xffff : 0; break;
    case 0x66: for (i = 0; i < 4; i++) d->d[i] = t.sd[i] > s->sd[i] ? 0xffffffffu : 0; break;
    case 0x67: for (i = 0; i < 8; i++) { int v = t.sw[i]; d->b[i] = (u8)(v > 255 ? 255 : v < 0 ? 0 : v); int w = s->sw[i]; d->b[8 + i] = (u8)(w > 255 ? 255 : w < 0 ? 0 : w); } break;
    case 0x68: for (i = 0; i < 8; i++) { d->b[2 * i] = t.b[8 + i]; d->b[2 * i + 1] = s->b[8 + i]; } break;
    case 0x69: for (i = 0; i < 4; i++) { d->w[2 * i] = t.w[4 + i]; d->w[2 * i + 1] = s->w[4 + i]; } break;
    case 0x6A: for (i = 0; i < 2; i++) { d->d[2 * i] = t.d[2 + i]; d->d[2 * i + 1] = s->d[2 + i]; } break;
    case 0x6B: for (i = 0; i < 4; i++) { s64 v = t.sd[i]; d->sw[i] = (s16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); s64 w = s->sd[i]; d->sw[4 + i] = (s16)(w > 32767 ? 32767 : w < -32768 ? -32768 : w); } break;
    case 0x6C: d->q[0] = t.q[0]; d->q[1] = s->q[0]; break;
    case 0x6D: d->q[0] = t.q[1]; d->q[1] = s->q[1]; break;
    case 0x74: for (i = 0; i < 16; i++) d->b[i] = t.b[i] == s->b[i] ? 0xff : 0; break;
    case 0x75: for (i = 0; i < 8; i++) d->w[i] = t.w[i] == s->w[i] ? 0xffff : 0; break;
    case 0x76: for (i = 0; i < 4; i++) d->d[i] = t.d[i] == s->d[i] ? 0xffffffffu : 0; break;
    case 0xD1: { u64 n = s->q[0]; for (i = 0; i < 8; i++) d->w[i] = n > 15 ? 0 : (u16)(t.w[i] >> n); break; }
    case 0xD2: { u64 n = s->q[0]; for (i = 0; i < 4; i++) d->d[i] = n > 31 ? 0 : t.d[i] >> n; break; }
    case 0xD3: { u64 n = s->q[0]; for (i = 0; i < 2; i++) d->q[i] = n > 63 ? 0 : t.q[i] >> n; break; }
    case 0xD4: d->q[0] = t.q[0] + s->q[0]; d->q[1] = t.q[1] + s->q[1]; break;
    case 0xD5: for (i = 0; i < 8; i++) d->w[i] = (u16)(t.sw[i] * s->sw[i]); break;
    case 0xD8: for (i = 0; i < 16; i++) d->b[i] = t.b[i] > s->b[i] ? t.b[i] - s->b[i] : 0; break;
    case 0xD9: for (i = 0; i < 8; i++) d->w[i] = t.w[i] > s->w[i] ? t.w[i] - s->w[i] : 0; break;
    case 0xDA: for (i = 0; i < 16; i++) d->b[i] = t.b[i] < s->b[i] ? t.b[i] : s->b[i]; break;
    case 0xDB: d->q[0] = t.q[0] & s->q[0]; d->q[1] = t.q[1] & s->q[1]; break;
    case 0xDC: for (i = 0; i < 16; i++) { int v = t.b[i] + s->b[i]; d->b[i] = (u8)(v > 255 ? 255 : v); } break;
    case 0xDD: for (i = 0; i < 8; i++) { int v = t.w[i] + s->w[i]; d->w[i] = (u16)(v > 65535 ? 65535 : v); } break;
    case 0xDE: for (i = 0; i < 16; i++) d->b[i] = t.b[i] > s->b[i] ? t.b[i] : s->b[i]; break;
    case 0xDF: d->q[0] = ~t.q[0] & s->q[0]; d->q[1] = ~t.q[1] & s->q[1]; break;
    case 0xE0: for (i = 0; i < 16; i++) d->b[i] = (u8)((t.b[i] + s->b[i] + 1) >> 1); break;
    case 0xE1: { u64 n = s->q[0]; for (i = 0; i < 8; i++) d->sw[i] = (s16)(t.sw[i] >> (n > 15 ? 15 : n)); break; }
    case 0xE2: { u64 n = s->q[0]; for (i = 0; i < 4; i++) d->sd[i] = t.sd[i] >> (n > 31 ? 31 : n); break; }
    case 0xE3: for (i = 0; i < 8; i++) d->w[i] = (u16)((t.w[i] + s->w[i] + 1) >> 1); break;
    case 0xE4: for (i = 0; i < 8; i++) d->w[i] = (u16)(((u32) t.w[i] * s->w[i]) >> 16); break;
    case 0xE5: for (i = 0; i < 8; i++) d->w[i] = (u16)(((s32) t.sw[i] * s->sw[i]) >> 16); break;
    case 0xE8: for (i = 0; i < 16; i++) { int v = t.sb[i] - s->sb[i]; d->sb[i] = (s8)(v > 127 ? 127 : v < -128 ? -128 : v); } break;
    case 0xE9: for (i = 0; i < 8; i++) { int v = t.sw[i] - s->sw[i]; d->sw[i] = (s16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); } break;
    case 0xEA: for (i = 0; i < 8; i++) d->sw[i] = t.sw[i] < s->sw[i] ? t.sw[i] : s->sw[i]; break;
    case 0xEB: d->q[0] = t.q[0] | s->q[0]; d->q[1] = t.q[1] | s->q[1]; break;
    case 0xEC: for (i = 0; i < 16; i++) { int v = t.sb[i] + s->sb[i]; d->sb[i] = (s8)(v > 127 ? 127 : v < -128 ? -128 : v); } break;
    case 0xED: for (i = 0; i < 8; i++) { int v = t.sw[i] + s->sw[i]; d->sw[i] = (s16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); } break;
    case 0xEE: for (i = 0; i < 8; i++) d->sw[i] = t.sw[i] > s->sw[i] ? t.sw[i] : s->sw[i]; break;
    case 0xEF: d->q[0] = t.q[0] ^ s->q[0]; d->q[1] = t.q[1] ^ s->q[1]; break;
    case 0xF1: { u64 n = s->q[0]; for (i = 0; i < 8; i++) d->w[i] = n > 15 ? 0 : (u16)(t.w[i] << n); break; }
    case 0xF2: { u64 n = s->q[0]; for (i = 0; i < 4; i++) d->d[i] = n > 31 ? 0 : t.d[i] << n; break; }
    case 0xF3: { u64 n = s->q[0]; for (i = 0; i < 2; i++) d->q[i] = n > 63 ? 0 : t.q[i] << n; break; }
    case 0xF4: d->q[0] = (u64) t.d[0] * s->d[0]; d->q[1] = (u64) t.d[2] * s->d[2]; break;
    case 0xF5: for (i = 0; i < 4; i++) d->sd[i] = t.sw[2 * i] * s->sw[2 * i] + t.sw[2 * i + 1] * s->sw[2 * i + 1]; break;
    case 0xF6: for (int h = 0; h < 2; h++) { u32 sum = 0; for (i = 0; i < 8; i++) sum += (u32)abs((int) t.b[8 * h + i] - (int) s->b[8 * h + i]); d->q[h] = sum; } break;
    case 0xF8: for (i = 0; i < 16; i++) d->b[i] = (u8)(t.b[i] - s->b[i]); break;
    case 0xF9: for (i = 0; i < 8; i++) d->w[i] = (u16)(t.w[i] - s->w[i]); break;
    case 0xFA: for (i = 0; i < 4; i++) d->d[i] = t.d[i] - s->d[i]; break;
    case 0xFB: d->q[0] = t.q[0] - s->q[0]; d->q[1] = t.q[1] - s->q[1]; break;
    case 0xFC: for (i = 0; i < 16; i++) d->b[i] = (u8)(t.b[i] + s->b[i]); break;
    case 0xFD: for (i = 0; i < 8; i++) d->w[i] = (u16)(t.w[i] + s->w[i]); break;
    case 0xFE: for (i = 0; i < 4; i++) d->d[i] = t.d[i] + s->d[i]; break;
    default: *ok = 0;
    }
}
static u64 cvt_i(double v, int sz, int tr) {
    double r = tr ? trunc(v) : nearbyint(v);
    if (isnan(r) || (sz == 8 ? (r >= 9223372036854775808.0 || r < -9223372036854775808.0) : (r >= 2147483648.0 || r < -2147483648.0)))
        return sz == 8 ? 0x8000000000000000ULL : 0x80000000ULL;
    return sz == 8 ? (u64)(s64) r : (u64)(u32)(s32) r;
}

#define SRC(n) do { if (m.ismem) { memset(&sv, 0, 16); memcpy(&sv, (void*) m.ea, (size_t)(n)); } else sv = c->x[m.rm]; s = &sv; } while (0)
#define FARITH(T, SQRT, FLD, N) for (int i = 0; i < (N); i++) { T a = d->FLD[i], b = s->FLD[i], r; \
    switch (op) { case 0x51: r = SQRT(b); break; case 0x58: r = a + b; break; case 0x59: r = a * b; break; \
    case 0x5C: r = a - b; break; case 0x5D: r = a < b ? a : b; break; case 0x5E: r = a / b; break; default: r = a > b ? a : b; } d->FLD[i] = r; }

static int cmp_pred(int p, double a, double b) {
    int un = isnan(a) || isnan(b);
    switch (p & 7) {
    case 0: return !un && a == b; case 1: return !un && a < b; case 2: return !un && a <= b; case 3: return un;
    case 4: return un || a != b; case 5: return un || !(a < b); case 6: return un || !(a <= b); default: return !un;
    }
}

static int sse(CPU*c, I*in, int op) {
    int pf = in->rep ? 3 : in->repne ? 2 : in->p66 ? 1 : 0, imm8 = 0, ok = 1;
    M m; V sv, *s = NULL;
    int immb = (op == 0x70 || op == 0xC2 || op == 0xC4 || op == 0xC5 || op == 0xC6 || (op >= 0x71 && op <= 0x73)) ? 1 : 0;
    modrm(c, in, &m, immb);
    if (immb) imm8 = *in->ip++;
    V*d = &c->x[m.reg];
    switch (op) {
    case 0x10:
        if (pf <= 1) { SRC(16); *d = *s; }
        else if (m.ismem) { d->q[0] = d->q[1] = 0; memcpy(d, (void*) m.ea, pf == 3 ? 4 : 8); }
        else memcpy(d, &c->x[m.rm], pf == 3 ? 4 : 8);
        break;
    case 0x11:
        if (pf <= 1) { if (m.ismem) memcpy((void*) m.ea, d, 16); else c->x[m.rm] = *d; }
        else if (m.ismem) memcpy((void*) m.ea, d, pf == 3 ? 4 : 8);
        else memcpy(&c->x[m.rm], d, pf == 3 ? 4 : 8);
        break;
    case 0x12: if (m.ismem) d->q[0] = *(u64u*) m.ea; else d->q[0] = c->x[m.rm].q[1]; break;
    case 0x13: if (!m.ismem) return -1; *(u64u*) m.ea = d->q[0]; break;
    case 0x16: if (m.ismem) d->q[1] = *(u64u*) m.ea; else d->q[1] = c->x[m.rm].q[0]; break;
    case 0x17: if (!m.ismem) return -1; *(u64u*) m.ea = d->q[1]; break;
    case 0x14: SRC(16); if (pf == 0) { V t = *d; d->f[0] = t.f[0]; d->f[1] = s->f[0]; d->f[2] = t.f[1]; d->f[3] = s->f[1]; } else { d->q[1] = s->q[0]; } break;
    case 0x15: SRC(16); if (pf == 0) { V t = *d; d->f[0] = t.f[2]; d->f[1] = s->f[2]; d->f[2] = t.f[3]; d->f[3] = s->f[3]; } else { d->q[0] = d->q[1]; d->q[1] = s->q[1]; } break;
    case 0x28: case 0x6F: if (op == 0x6F && pf == 0) return -1; SRC(16); *d = *s; break;
    case 0x29: case 0x7F: case 0x2B: case 0xE7:
        if (m.ismem) memcpy((void*) m.ea, d, 16); else c->x[m.rm] = *d;
        break;
    case 0x2A: {
        u64 v = rdm(c, in, &m, in->w ? 8 : 4);
        if (pf == 3) d->f[0] = (float)(in->w ? (s64) v : (s64)(s32) v);
        else if (pf == 2) d->df[0] = (double)(in->w ? (s64) v : (s64)(s32) v);
        else return -1;
        break; }
    case 0x2C: case 0x2D: {
        int sz = in->w ? 8 : 4;
        if (pf == 3) { SRC(4); setr(c, m.reg, sz, 0, cvt_i((double) s->f[0], sz, op == 0x2C)); }
        else if (pf == 2) { SRC(8); setr(c, m.reg, sz, 0, cvt_i(s->df[0], sz, op == 0x2C)); }
        else return -1;
        break; }
    case 0x2E: case 0x2F: {
        double a, b;
        if (pf == 0) { SRC(4); a = d->f[0]; b = s->f[0]; } else if (pf == 1) { SRC(8); a = d->df[0]; b = s->df[0]; } else return -1;
        RF(c) &= ~(u64)(OF_ | SF_ | AF_ | ZF_ | PF_ | CF_);
        if (isnan(a) || isnan(b)) RF(c) |= ZF_ | PF_ | CF_; else if (a < b) RF(c) |= CF_; else if (a == b) RF(c) |= ZF_;
        break; }
    case 0x50: { if (m.ismem) return -1; V*r = &c->x[m.rm]; u64 v = 0;
        if (pf == 0) { for (int i = 0; i < 4; i++) v |= (u64)(r->d[i] >> 31) << i; } else { for (int i = 0; i < 2; i++) v |= (r->q[i] >> 63) << i; }
        setr(c, m.reg, 4, 0, v); break; }
    case 0x51: case 0x58: case 0x59: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        if (pf == 0) { SRC(16); FARITH(float, sqrtf, f, 4) }
        else if (pf == 1) { SRC(16); FARITH(double, sqrt, df, 2) }
        else if (pf == 3) { SRC(4); FARITH(float, sqrtf, f, 1) }
        else { SRC(8); FARITH(double, sqrt, df, 1) }
        break;
    case 0x54: case 0x55: case 0x56: case 0x57:
        SRC(16);
        for (int i = 0; i < 2; i++) { u64 a = d->q[i], b = s->q[i]; d->q[i] = op == 0x54 ? a & b : op == 0x55 ? ~a & b : op == 0x56 ? a | b : a ^ b; }
        break;
    case 0x5A:
        if (pf == 0) { SRC(8); double a = s->f[0], b = s->f[1]; d->df[0] = a; d->df[1] = b; }
        else if (pf == 1) { SRC(16); float a = (float) s->df[0], b = (float) s->df[1]; d->f[0] = a; d->f[1] = b; d->q[1] = 0; }
        else if (pf == 3) { SRC(4); d->df[0] = (double) s->f[0]; }
        else { SRC(8); d->f[0] = (float) s->df[0]; }
        break;
    case 0x5B:
        SRC(16);
        if (pf == 0) for (int i = 0; i < 4; i++) d->f[i] = (float) s->sd[i];
        else if (pf == 1 || pf == 3) for (int i = 0; i < 4; i++) d->sd[i] = (s32) cvt_i((double) s->f[i], 4, pf == 3);
        else return -1;
        break;
    case 0xE6:
        if (pf == 3) { SRC(8); double a = s->sd[0], b = s->sd[1]; d->df[0] = a; d->df[1] = b; }
        else if (pf == 2 || pf == 1) { SRC(16); s32 a = (s32) cvt_i(s->df[0], 4, pf == 1), b = (s32) cvt_i(s->df[1], 4, pf == 1); d->sd[0] = a; d->sd[1] = b; d->q[1] = 0; }
        else return -1;
        break;
    case 0xC2: {
        if (pf == 0) { SRC(16); for (int i = 0; i < 4; i++) d->d[i] = cmp_pred(imm8, d->f[i], s->f[i]) ? ~0u : 0; }
        else if (pf == 1) { SRC(16); for (int i = 0; i < 2; i++) d->q[i] = cmp_pred(imm8, d->df[i], s->df[i]) ? ~0ULL : 0; }
        else if (pf == 3) { SRC(4); d->d[0] = cmp_pred(imm8, d->f[0], s->f[0]) ? ~0u : 0; }
        else { SRC(8); d->q[0] = cmp_pred(imm8, d->df[0], s->df[0]) ? ~0ULL : 0; }
        break; }
    case 0xC6: SRC(16);
        if (pf == 0) { V t = *d; d->f[0] = t.f[imm8 & 3]; d->f[1] = t.f[(imm8 >> 2) & 3]; d->f[2] = s->f[(imm8 >> 4) & 3]; d->f[3] = s->f[(imm8 >> 6) & 3]; }
        else { V t = *d; d->q[0] = t.q[imm8 & 1]; d->q[1] = s->q[(imm8 >> 1) & 1]; }
        break;
    case 0x70: SRC(16);
        if (pf == 1) { for (int i = 0; i < 4; i++) d->d[i] = s->d[(imm8 >> (2 * i)) & 3]; }
        else if (pf == 2) { for (int i = 0; i < 4; i++) d->w[i] = s->w[(imm8 >> (2 * i)) & 3]; d->q[1] = s->q[1]; }
        else if (pf == 3) { for (int i = 0; i < 4; i++) d->w[4 + i] = s->w[4 + ((imm8 >> (2 * i)) & 3)]; d->q[0] = s->q[0]; }
        else return -1;
        break;
    case 0x71: case 0x72: case 0x73: {
        if (m.ismem || pf != 1) return -1;
        V*r = &c->x[m.rm]; int ext = m.reg & 7; unsigned n = (unsigned) imm8;
        if (op == 0x71) { for (int i = 0; i < 8; i++) { if (ext == 2) r->w[i] = n > 15 ? 0 : (u16)(r->w[i] >> n); else if (ext == 4) r->sw[i] = (s16)(r->sw[i] >> (n > 15 ? 15 : n)); else if (ext == 6) r->w[i] = n > 15 ? 0 : (u16)(r->w[i] << n); else return -1; } }
        else if (op == 0x72) { for (int i = 0; i < 4; i++) { if (ext == 2) r->d[i] = n > 31 ? 0 : r->d[i] >> n; else if (ext == 4) r->sd[i] = r->sd[i] >> (n > 31 ? 31 : n); else if (ext == 6) r->d[i] = n > 31 ? 0 : r->d[i] << n; else return -1; } }
        else {
            if (ext == 2 || ext == 6) { for (int i = 0; i < 2; i++) r->q[i] = n > 63 ? 0 : (ext == 2 ? r->q[i] >> n : r->q[i] << n); }
            else if (ext == 3 || ext == 7) { V t = *r; memset(r, 0, 16); if (n < 16) for (int i = 0; i < 16; i++) { int j = ext == 3 ? i + (int) n : i - (int) n; if (j >= 0 && j < 16) r->b[i] = t.b[j]; } }
            else return -1;
        }
        break; }
    case 0xC4: d->w[imm8 & 7] = (u16)(m.ismem ? mrd(m.ea, 2) : getr(c, m.rm, 4, 0)); break;
    case 0xC5: if (m.ismem) return -1; setr(c, m.reg, 4, 0, c->x[m.rm].w[imm8 & 7]); break;
    case 0xD7: { if (m.ismem) return -1; V*r = &c->x[m.rm]; u64 v = 0; for (int i = 0; i < 16; i++) v |= (u64)(r->b[i] >> 7) << i; setr(c, m.reg, 4, 0, v); break; }
    case 0xD6: if (pf != 1) return -1; if (m.ismem) *(u64u*) m.ea = d->q[0]; else { c->x[m.rm].q[0] = d->q[0]; c->x[m.rm].q[1] = 0; } break;
    case 0x6E: if (pf != 1) return -1; d->q[0] = rdm(c, in, &m, in->w ? 8 : 4); d->q[1] = 0; break;
    case 0x7E:
        if (pf == 1) wrm(c, in, &m, in->w ? 8 : 4, d->q[0]);
        else if (pf == 3) { SRC(8); d->q[0] = s->q[0]; d->q[1] = 0; }
        else return -1;
        break;
    case 0x77: break;
    default:
        if (pf != 1) return -1;
        SRC(16);
        pint(op, d, s, &ok);
        if (!ok) return -1;
    }
    return 0;
}

/* ------------------------------------------------------------ instruction step */
/* ------------------------------------------------------------ x87 (subset) */
/* Values are held as host long double: exact 80-bit on x86_64 hosts; IEEE quad
 * (slightly more precise) on aarch64 hosts. Memory format is always 80-bit. */
#define ST(i) (c->st[(c->top + (i)) & 7])
static long double ld_from80(const u8*p) {
    u64 m = *(u64u*) p; u16 se = *(u16u*)(p + 8); int e = se & 0x7fff; long double v;
    if (e == 0x7fff) v = (m << 1) ? (long double) NAN : (long double) INFINITY;
    else if (!m) v = 0;
    else v = ldexpl((long double) m, (e ? e : 1) - 16383 - 63);
    return (se >> 15) ? -v : v;
}
static void ld_to80(long double v, u8*p) {
    u64 m = 0; u16 se = 0;
    if (signbit(v)) { se = 0x8000; v = -v; }
    if (isnan(v)) { se |= 0x7fff; m = 0xC000000000000000ULL; }
    else if (isinf(v)) { se |= 0x7fff; m = 0x8000000000000000ULL; }
    else if (v != 0) {
        int e; long double f = frexpl(v, &e);
        long double sc = nearbyintl(ldexpl(f, 64));
        if (sc >= 18446744073709551616.0L) { m = 0x8000000000000000ULL; e++; } else m = (u64) sc;
        int be = e - 1 + 16383;
        if (be <= 0) { m = 0; be = 0; } else if (be >= 0x7fff) { be = 0x7fff; m = 0x8000000000000000ULL; }
        se |= (u16) be;
    }
    *(u64u*) p = m; *(u16u*)(p + 8) = se;
}
static void xpush(CPU*c, long double v) { c->top = (c->top - 1) & 7; c->st[c->top] = v; }
static void xpop(CPU*c) { c->top = (c->top + 1) & 7; }
static long double xround(CPU*c, long double v) {
    switch ((c->fcw >> 10) & 3) { case 1: return floorl(v); case 2: return ceill(v); case 3: return truncl(v); default: return nearbyintl(v); }
}
static long double xop(int r, long double a, long double b) {
    switch (r) { case 0: return a + b; case 1: return a * b; case 4: return a - b; case 5: return b - a; case 6: return a / b; default: return b / a; }
}
static void x_cc(CPU*c, long double a, long double b) {
    c->fsw &= ~0x4500;
    if (isnan(a) || isnan(b)) c->fsw |= 0x4500; else if (a < b) c->fsw |= 0x0100; else if (a == b) c->fsw |= 0x4000;
}
static void x_flags(CPU*c, long double a, long double b) {
    RF(c) &= ~(u64)(OF_ | SF_ | AF_ | ZF_ | PF_ | CF_);
    if (isnan(a) || isnan(b)) RF(c) |= ZF_ | PF_ | CF_; else if (a < b) RF(c) |= CF_; else if (a == b) RF(c) |= ZF_;
}
static u64 x_toint(CPU*c, long double v, int sz) {
    long double r = xround(c, v), lim = sz == 2 ? 32768.0L : sz == 4 ? 2147483648.0L : 9223372036854775808.0L;
    if (isnan(r) || r >= lim || r < -lim) return sz == 2 ? 0x8000 : sz == 4 ? 0x80000000ULL : 0x8000000000000000ULL;
    return (u64)(s64) r;
}
static u64 x_ld_int(u64 raw, int sz) { return (u64) sx(raw, sz); }
static int x87(CPU*c, I*in, u8 op) {
    M m; modrm(c, in, &m, 0);
    int r = m.reg & 7, i = m.rm & 7;
    if (m.ismem) {
        u64 ea = m.ea; long double v;
        switch (op) {
        case 0xD8: case 0xDC: case 0xDA: case 0xDE: {
            if (op == 0xD8) { float f; memcpy(&f, (void*) ea, 4); v = f; }
            else if (op == 0xDC) { double d; memcpy(&d, (void*) ea, 8); v = d; }
            else if (op == 0xDA) v = (long double)(s32) mrd(ea, 4);
            else v = (long double)(s16) mrd(ea, 2);
            if (r == 2 || r == 3) { x_cc(c, ST(0), v); if (r == 3) xpop(c); }
            else ST(0) = xop(r, ST(0), v);
            return 0; }
        case 0xD9:
            switch (r) {
            case 0: { float f; memcpy(&f, (void*) ea, 4); xpush(c, f); return 0; }
            case 2: case 3: { float f = (float) ST(0); memcpy((void*) ea, &f, 4); if (r == 3) xpop(c); return 0; }
            case 4: c->fcw = (u16) mrd(ea, 2); c->fsw = (u16)(mrd(ea + 4, 2) & 0x4700); c->top = (int)((mrd(ea + 4, 2) >> 11) & 7); return 0;
            case 5: c->fcw = (u16) mrd(ea, 2); return 0;
            case 6: memset((void*) ea, 0, 28); mwr(ea, c->fcw, 2); mwr(ea + 4, (u64)((c->fsw & ~0x3800) | (c->top << 11)), 2); mwr(ea + 8, 0xffff, 2); return 0;
            case 7: mwr(ea, c->fcw, 2); return 0;
            }
            return -1;
        case 0xDD:
            switch (r) {
            case 0: { double d; memcpy(&d, (void*) ea, 8); xpush(c, d); return 0; }
            case 2: case 3: { double d = (double) ST(0); memcpy((void*) ea, &d, 8); if (r == 3) xpop(c); return 0; }
            case 7: mwr(ea, (u64)((c->fsw & ~0x3800) | (c->top << 11)), 2); return 0;
            }
            return -1;
        case 0xDB:
            switch (r) {
            case 0: xpush(c, (long double)(s32) mrd(ea, 4)); return 0;
            case 2: case 3: mwr(ea, x_toint(c, ST(0), 4), 4); if (r == 3) xpop(c); return 0;
            case 5: xpush(c, ld_from80((u8*) ea)); return 0;
            case 7: ld_to80(ST(0), (u8*) ea); xpop(c); return 0;
            }
            return -1;
        case 0xDF:
            switch (r) {
            case 0: xpush(c, (long double)(s16) mrd(ea, 2)); return 0;
            case 2: case 3: mwr(ea, x_toint(c, ST(0), 2), 2); if (r == 3) xpop(c); return 0;
            case 5: xpush(c, (long double)(s64) mrd(ea, 8)); return 0;
            case 7: mwr(ea, x_toint(c, ST(0), 8), 8); xpop(c); return 0;
            }
            return -1;
        }
        (void) x_ld_int;
        return -1;
    }
    switch (op) {
    case 0xD8: if (r == 2 || r == 3) { x_cc(c, ST(0), ST(i)); if (r == 3) xpop(c); } else ST(0) = xop(r, ST(0), ST(i)); return 0;
    case 0xDC: case 0xDE: {
        if (op == 0xDE && r == 3 && i == 1) { x_cc(c, ST(0), ST(1)); xpop(c); xpop(c); return 0; }          /* fcompp */
        if (r == 2 || r == 3) return -1;
        int rr = r >= 4 ? (r ^ 1) : r;
        ST(i) = xop(rr, ST(i), ST(0));
        if (op == 0xDE) xpop(c);
        return 0; }
    case 0xD9:
        switch (r) {
        case 0: xpush(c, ST(i)); return 0;
        case 1: { long double t = ST(0); ST(0) = ST(i); ST(i) = t; return 0; }
        case 2: return i == 0 ? 0 : -1;
        case 4:
            switch (i) { case 0: ST(0) = -ST(0); return 0; case 1: ST(0) = fabsl(ST(0)); return 0; case 4: x_cc(c, ST(0), 0.0L); return 0;
            case 5: { long double v = ST(0); c->fsw &= ~0x4700; if (signbit(v)) c->fsw |= 0x200;
                      switch (fpclassify(v)) { case FP_NAN: c->fsw |= 0x100; break; case FP_INFINITE: c->fsw |= 0x500; break; case FP_ZERO: c->fsw |= 0x4000; break; case FP_SUBNORMAL: c->fsw |= 0x4400; break; default: c->fsw |= 0x400; } return 0; } }
            return -1;
        case 5:
            switch (i) { case 0: xpush(c, 1.0L); return 0; case 1: xpush(c, 3.321928094887362347870319429489390175864831393L); return 0;
            case 2: xpush(c, 1.442695040888963407359924681001892137L); return 0; case 3: xpush(c, 3.141592653589793238462643383279502884L); return 0;
            case 4: xpush(c, 0.301029995663981195213738894724493026768189881L); return 0; case 5: xpush(c, 0.693147180559945309417232121458176568L); return 0;
            case 6: xpush(c, 0.0L); return 0; }
            return -1;
        case 6:
            switch (i) {
            case 0: ST(0) = expm1l(ST(0) * 0.693147180559945309417232121458176568L); return 0;
            case 1: { long double x = ST(0), y = ST(1); xpop(c); ST(0) = y * log2l(x); return 0; }
            case 2: { long double v = ST(0); ST(0) = tanl(v); xpush(c, 1.0L); c->fsw &= ~0x400; return 0; }
            case 3: { long double x = ST(0), y = ST(1); xpop(c); ST(0) = atan2l(y, x); return 0; }
            case 4: { long double v = ST(0); int e; if (v == 0) { ST(0) = -INFINITY; xpush(c, v); return 0; } long double f = frexpl(v, &e); ST(0) = (long double)(e - 1); xpush(c, f * 2); return 0; }
            case 5: { long double a = ST(0), b = ST(1); ST(0) = remainderl(a, b); c->fsw &= ~0x400; return 0; }
            case 6: c->top = (c->top - 1) & 7; return 0;
            case 7: c->top = (c->top + 1) & 7; return 0;
            }
            return -1;
        case 7:
            switch (i) {
            case 0: { long double a = ST(0), b = ST(1); ST(0) = fmodl(a, b); c->fsw &= ~0x400; return 0; }
            case 1: { long double x = ST(0), y = ST(1); xpop(c); ST(0) = y * log2l(x + 1.0L); return 0; }
            case 2: ST(0) = sqrtl(ST(0)); return 0;
            case 3: { long double v = ST(0); ST(0) = sinl(v); xpush(c, cosl(v)); c->fsw &= ~0x400; return 0; }
            case 4: ST(0) = xround(c, ST(0)); return 0;
            case 5: ST(0) = ldexpl(ST(0), (int) truncl(ST(1))); return 0;
            case 6: ST(0) = sinl(ST(0)); c->fsw &= ~0x400; return 0;
            case 7: ST(0) = cosl(ST(0)); c->fsw &= ~0x400; return 0;
            }
            return -1;
        }
        return -1;
    case 0xDA:
        if (r == 5 && i == 1) { x_cc(c, ST(0), ST(1)); xpop(c); xpop(c); return 0; }                 /* fucompp */
        if (r < 4) { int cc = r == 0 ? (RF(c) & CF_) : r == 1 ? (RF(c) & ZF_) : r == 2 ? (RF(c) & (CF_ | ZF_)) : (RF(c) & PF_); if (cc) ST(0) = ST(i); return 0; }
        return -1;
    case 0xDB:
        if (r < 4) { int cc = r == 0 ? (RF(c) & CF_) : r == 1 ? (RF(c) & ZF_) : r == 2 ? (RF(c) & (CF_ | ZF_)) : (RF(c) & PF_); if (!cc) ST(0) = ST(i); return 0; }
        if (r == 4 && i == 2) { c->fsw = 0; return 0; }
        if (r == 4 && i == 3) { c->fcw = 0x37f; c->fsw = 0; c->top = 0; return 0; }
        if (r == 5 || r == 6) { x_flags(c, ST(0), ST(i)); return 0; }
        return -1;
    case 0xDD:
        if (r == 0) return 0;                                                                         /* ffree */
        if (r == 2 || r == 3) { ST(i) = ST(0); if (r == 3) xpop(c); return 0; }
        if (r == 4 || r == 5) { x_cc(c, ST(0), ST(i)); if (r == 5) xpop(c); return 0; }
        return -1;
    case 0xDF:
        if (r == 4 && i == 0) { setr(c, RAX, 2, 0, (u64)((c->fsw & ~0x3800) | (c->top << 11))); return 0; }
        if (r == 5 || r == 6) { x_flags(c, ST(0), ST(i)); xpop(c); return 0; }
        return -1;
    }
    return -1;
}

static void do_div(CPU*c, int sz, u64 src, int sgd, const u8*ip) {
    int bits = sz * 8;
    if (sz == 1) {                                  /* AX / src8 */
        u64 ax = c->r[RAX] & 0xffff;
        if (!(src & 0xff)) { raise(SIGFPE); return; }
        if (sgd) { s16 n = (s16) ax; s8 dv = (s8) src; if (dv == -1 && n == -32768) { raise(SIGFPE); return; } s32 q = n / dv, r = n % dv; if (q > 127 || q < -128) { raise(SIGFPE); return; } c->r[RAX] = (c->r[RAX] & ~0xffffULL) | (u8) q | ((u64)(u8) r << 8); }
        else { u32 q = (u32) ax / (u8) src, r = (u32) ax % (u8) src; if (q > 255) { raise(SIGFPE); return; } c->r[RAX] = (c->r[RAX] & ~0xffffULL) | q | (r << 8); }
        return;
    }
    u64 m = mskz(sz); src &= m;
    if (!src) { raise(SIGFPE); return; }
    unsigned __int128 un = ((unsigned __int128)(c->r[RDX] & m) << bits) | (c->r[RAX] & m);
    if (sgd) {
        __int128 n = (__int128) un; if (sz < 8) n = (__int128)((s64)(un << (64 - 2 * bits)) >> (64 - 2 * bits)); else n = (__int128) un;
        __int128 dv = sx(src, sz), q, r;
        if (dv == -1 && n == -((__int128) 1 << (bits - 1))) { raise(SIGFPE); return; }
        q = n / dv; r = n % dv;
        if (q > (((__int128) 1 << (bits - 1)) - 1) || q < -((__int128) 1 << (bits - 1))) { raise(SIGFPE); return; }
        setr(c, RAX, sz, 0, (u64) q); setr(c, RDX, sz, 0, (u64) r);
    } else {
        unsigned __int128 q = un / src, r = un % src;
        if (q > m) { raise(SIGFPE); return; }
        setr(c, RAX, sz, 0, (u64) q); setr(c, RDX, sz, 0, (u64) r);
    }
    (void) ip;
}

static void cpuid(CPU*c) {
    u32 leaf = (u32) c->r[RAX], sub = (u32) c->r[RCX], a = 0, b = 0, cc = 0, d = 0;
    if (leaf == 0) { a = 4; b = 0x756e6547; d = 0x49656e69; cc = 0x6c65746e; }          /* "GenuineIntel": glibc only reads leaf 1 for known vendors */
    else if (leaf == 1) { a = 0x000306a9; b = 0x00010800; cc = 0; d = (1 << 0) | (1 << 4) | (1 << 8) | (1 << 15) | (1 << 23) | (1 << 24) | (1 << 25) | (1 << 26); }
    else if (leaf == 2) { a = 1; }                                                      /* one round, no cache descriptors */
    else if (leaf == 4) {                                                               /* deterministic cache parameters */
        static const u32 e[4][4] = {
            {(1 << 5) | 1, (7u << 22) | 63, 63, 0},          /* L1d 32K 8-way */
            {(1 << 5) | 2, (7u << 22) | 63, 63, 0},          /* L1i 32K 8-way */
            {(2 << 5) | 3, (7u << 22) | 63, 511, 0},         /* L2 256K 8-way */
            {(3 << 5) | 3 | (15u << 14), (15u << 22) | 63, 8191, 0}, /* L3 8M 16-way */
        };
        if (sub < 4) { a = e[sub][0]; b = e[sub][1]; cc = e[sub][2]; d = e[sub][3]; }
    }
    else if (leaf == 0x80000000u) { a = 0x80000001u; }
    else if (leaf == 0x80000001u) { cc = 1; d = (1 << 11) | (1 << 20) | (1 << 29); }
    c->r[RAX] = a; c->r[RBX] = b; c->r[RCX] = cc; c->r[RDX] = d;
}

/* in/out/cli/sti/hlt are privileged: a real CPU raises #GP and Linux delivers SIGSEGV
 * (si_code SI_KERNEL, rip = the instruction). Programs such as lscpu probe the VMware
 * backdoor port this way and catch the signal. */
static void gp_fault(CPU*c) {
    if (gsa[SIGSEGV].handler > 1) {
        memset(&pinfo[SIGSEGV], 0, sizeof pinfo[SIGSEGV]);
        pinfo[SIGSEGV].si_signo = SIGSEGV; pinfo[SIGSEGV].si_code = 0x80;
        g_fault = 0;
        deliver_sig(c, SIGSEGV);
        return;
    }
    signal(SIGSEGV, SIG_DFL); raise(SIGSEGV); _exit(128 + SIGSEGV);
}
static void step(CPU*c) {
    I in; memset(&in, 0, sizeof in);
    u8*start = (u8*) c->rip, *ip = start;
    M m;
    for (;;) {
        u8 b = *ip;
        if (b == 0x66) in.p66 = 1; else if (b == 0xF3) in.rep = 1; else if (b == 0xF2) in.repne = 1;
        else if (b == 0x64) in.seg = c->fs; else if (b == 0x65) in.seg = c->gs; else if (b == 0x67) in.addr32 = 1;
        else if (b == 0x2E || b == 0x3E || b == 0x26 || b == 0x36 || b == 0xF0) { }
        else break;
        ip++;
    }
    if ((*ip & 0xF0) == 0x40) in.rex = *ip++;
    in.w = (in.rex >> 3) & 1; in.ip = ip;
    int opsz = in.w ? 8 : in.p66 ? 2 : 4;
    int rexB = (in.rex & 1) << 3;
    u8 op = *in.ip++;

    if (op < 0x40 && (op & 7) < 6) {
        int t = op >> 3, f = op & 7;
        if (f < 4) {
            int sz = (f & 1) ? opsz : 1;
            modrm(c, &in, &m, 0);
            if (f < 2) { u64 r = alu(c, t, rdm(c, &in, &m, sz), getr(c, m.reg, sz, in.rex), sz); if (t != 7) wrm(c, &in, &m, sz, r); }
            else { u64 r = alu(c, t, getr(c, m.reg, sz, in.rex), rdm(c, &in, &m, sz), sz); if (t != 7) setr(c, m.reg, sz, in.rex, r); }
        } else {
            int sz = f == 4 ? 1 : opsz;
            u64 imm = sz == 1 ? (u64)(s64)(s8) *in.ip++ : immz(&in, opsz);
            u64 r = alu(c, t, getr(c, RAX, sz, 0), imm, sz);
            if (t != 7) setr(c, RAX, sz, 0, r);
        }
        c->rip = (u64) in.ip; return;
    }
    switch (op) {
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
        { u64 v = c->r[(op & 7) | rexB]; push(c, v); break; }
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        { u64 v = pop(c); c->r[(op & 7) | rexB] = v; break; }
    case 0x63: modrm(c, &in, &m, 0); { u64 v = rdm(c, &in, &m, 4); setr(c, m.reg, opsz, in.rex, opsz == 8 ? (u64) sx(v, 4) : v); } break;
    case 0x68: push(c, (u64)(s64) imm32(&in)); break;
    case 0x6A: push(c, (u64)(s64)(s8) *in.ip++); break;
    case 0x69: case 0x6B: {
        modrm(c, &in, &m, op == 0x69 ? (opsz == 2 ? 2 : 4) : 1);
        s64 a = sx(rdm(c, &in, &m, opsz), opsz), b = op == 0x69 ? (s64) immz(&in, opsz) : (s64)(s8) *in.ip++;
        __int128 p = (__int128) a * b; u64 r = (u64) p & mskz(opsz);
        int ov = p != (__int128) sx(r, opsz);
        RF(c) &= ~(u64)(CF_ | OF_); if (ov) RF(c) |= CF_ | OF_;
        setr(c, m.reg, opsz, in.rex, r); break; }
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
        s8 rel = (s8) *in.ip++;
        c->rip = (u64) in.ip + (u64)(s64) (cond(c, op & 15) ? rel : 0); return; }
    case 0x80: case 0x81: case 0x83: {
        int sz = op == 0x80 ? 1 : opsz;
        modrm(c, &in, &m, op == 0x81 ? (opsz == 2 ? 2 : 4) : 1);
        u64 imm = op == 0x81 ? immz(&in, opsz) : (u64)(s64)(s8) *in.ip++;
        u64 r = alu(c, m.reg & 7, rdm(c, &in, &m, sz), imm, sz);
        if ((m.reg & 7) != 7) wrm(c, &in, &m, sz, r);
        break; }
    case 0x84: case 0x85: { int sz = op & 1 ? opsz : 1; modrm(c, &in, &m, 0); f_logic(c, rdm(c, &in, &m, sz) & getr(c, m.reg, sz, in.rex), sz); break; }
    case 0x86: case 0x87: { int sz = op & 1 ? opsz : 1; modrm(c, &in, &m, 0); u64 a = rdm(c, &in, &m, sz), b = getr(c, m.reg, sz, in.rex); wrm(c, &in, &m, sz, b); setr(c, m.reg, sz, in.rex, a); break; }
    case 0x88: case 0x89: { int sz = op & 1 ? opsz : 1; modrm(c, &in, &m, 0); wrm(c, &in, &m, sz, getr(c, m.reg, sz, in.rex)); break; }
    case 0x8A: case 0x8B: { int sz = op & 1 ? opsz : 1; modrm(c, &in, &m, 0); setr(c, m.reg, sz, in.rex, rdm(c, &in, &m, sz)); break; }
    case 0x8D: { in.seg = 0; modrm(c, &in, &m, 0); if (!m.ismem) goto bad; setr(c, m.reg, opsz, in.rex, m.ea); break; }
    case 0x8F: { modrm(c, &in, &m, 0); u64 v = pop(c); if (m.ismem && (m.rm == RSP)) {} wrm(c, &in, &m, 8, v); break; }
    case 0x90:
        if (rexB) { u64 t = c->r[8]; c->r[8] = c->r[RAX]; c->r[RAX] = t; }
        break;
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
        int r = (op & 7) | rexB; u64 a = getr(c, RAX, opsz, 0), b = getr(c, r, opsz, 0);
        setr(c, RAX, opsz, 0, b); setr(c, r, opsz, 0, a); break; }
    case 0x98: if (opsz == 8) c->r[RAX] = (u64)(s64)(s32) c->r[RAX]; else if (opsz == 4) c->r[RAX] = (u32)(s32)(s16) c->r[RAX]; else setr(c, RAX, 2, 0, (u64)(s64)(s8) c->r[RAX]); break;
    case 0x99: if (opsz == 8) c->r[RDX] = (s64) c->r[RAX] < 0 ? ~0ULL : 0; else if (opsz == 4) c->r[RDX] = (s32) c->r[RAX] < 0 ? 0xffffffffULL : 0; else setr(c, RDX, 2, 0, (s16) c->r[RAX] < 0 ? 0xffff : 0); break;
    case 0x9B: break;                                         /* fwait */
    case 0x9C: push(c, RF(c) | 2); break;
    case 0x9D: { u64 v = pop(c); RF(c) = (v & (CF_ | PF_ | AF_ | ZF_ | SF_ | DF_ | OF_)) | 0x202; break; }
    case 0x9E: { u64 ah = (c->r[RAX] >> 8) & 0xff; RF(c) = (RF(c) & ~(u64)(CF_ | PF_ | AF_ | ZF_ | SF_)) | (ah & (CF_ | PF_ | AF_ | ZF_ | SF_)); break; }
    case 0x9F: setr(c, 4, 1, 0, (RF(c) & (CF_ | PF_ | AF_ | ZF_ | SF_)) | 2); break;
    case 0xA4: case 0xA5: case 0xA6: case 0xA7: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF: {
        int sz = (op & 1) ? opsz : 1; s64 dl = (RF(c) & DF_) ? -sz : sz;
        int isrep = in.rep || in.repne, kind = op >= 0xAE ? 4 : op >= 0xAC ? 3 : op >= 0xAA ? 2 : op >= 0xA6 ? 1 : 0;
        u64 cnt = isrep ? c->r[RCX] : 1;
        if (kind == 0 && isrep && !(RF(c) & DF_) && cnt) {
            u64 n = cnt * (u64) sz, dst = c->r[RDI], src = c->r[RSI];
            if (dst > src && dst < src + n) { for (u64 i = 0; i < n; i++) *(u8*)(dst + i) = *(u8*)(src + i); } else memmove((void*) dst, (void*) src, n);
            c->r[RDI] += n; c->r[RSI] += n; c->r[RCX] = 0; break;
        }
        if (kind == 2 && isrep && !(RF(c) & DF_) && sz == 1 && cnt) { memset((void*) c->r[RDI], (int)(c->r[RAX] & 0xff), cnt); c->r[RDI] += cnt; c->r[RCX] = 0; break; }
        while (cnt) {
            switch (kind) {
            case 0: mwr(c->r[RDI], mrd(c->r[RSI], sz), sz); c->r[RSI] += (u64) dl; c->r[RDI] += (u64) dl; break;
            case 1: f_sub(c, mrd(c->r[RSI], sz), mrd(c->r[RDI], sz), 0, sz); c->r[RSI] += (u64) dl; c->r[RDI] += (u64) dl; break;
            case 2: mwr(c->r[RDI], c->r[RAX], sz); c->r[RDI] += (u64) dl; break;
            case 3: setr(c, RAX, sz, 0, mrd(c->r[RSI], sz)); c->r[RSI] += (u64) dl; break;
            default: f_sub(c, getr(c, RAX, sz, 0), mrd(c->r[RDI], sz), 0, sz); c->r[RDI] += (u64) dl;
            }
            cnt--; if (isrep) c->r[RCX]--;
            if (isrep && (kind == 1 || kind == 4)) { int zf = !!(RF(c) & ZF_); if ((in.rep && !zf) || (in.repne && zf)) break; }
        }
        break; }
    case 0xA8: { u64 imm = (u64)(u8) *in.ip++; f_logic(c, c->r[RAX] & imm, 1); break; }
    case 0xA9: { u64 imm = immz(&in, opsz); f_logic(c, getr(c, RAX, opsz, 0) & imm, opsz); break; }
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        setr(c, (op & 7) | rexB, 1, in.rex, *in.ip++); break;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF: {
        int r = (op & 7) | rexB; u64 v;
        if (in.w) { v = *(u64u*) in.ip; in.ip += 8; } else if (in.p66) { v = *(u16u*) in.ip; in.ip += 2; } else { v = *(u32u*) in.ip; in.ip += 4; }
        setr(c, r, opsz, 0, v); break; }
    case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
        int sz = (op & 1) ? opsz : 1;
        modrm(c, &in, &m, (op <= 0xC1) ? 1 : 0);
        unsigned n = op <= 0xC1 ? *in.ip++ : op <= 0xD1 ? 1 : (unsigned)(c->r[RCX] & 0xff);
        u64 r = shiftop(c, m.reg & 7, rdm(c, &in, &m, sz), n, sz);
        wrm(c, &in, &m, sz, r); break; }
    case 0xC2: { u16 n = *(u16u*) in.ip; u64 ra = pop(c); c->r[RSP] += n; c->rip = ra; return; }
    case 0xC3: c->rip = pop(c); return;
    case 0xC6: case 0xC7: {
        int sz = op & 1 ? opsz : 1;
        modrm(c, &in, &m, sz == 1 ? 1 : (opsz == 2 ? 2 : 4));
        if ((m.reg & 7) != 0) goto bad;
        u64 imm = sz == 1 ? *in.ip++ : immz(&in, opsz);
        wrm(c, &in, &m, sz, imm); break; }
    case 0xC9: c->r[RSP] = c->r[RBP]; c->r[RBP] = pop(c); break;
    case 0xCC: c->rip = (u64) in.ip; raise(SIGTRAP); return;
    case 0xE8: { s32 rel = imm32(&in); push(c, (u64) in.ip); c->rip = (u64) in.ip + (u64)(s64) rel; return; }
    case 0xE9: { s32 rel = imm32(&in); c->rip = (u64) in.ip + (u64)(s64) rel; return; }
    case 0xEB: { s8 rel = (s8) *in.ip++; c->rip = (u64) in.ip + (u64)(s64) rel; return; }
    case 0xE3: { s8 rel = (s8) *in.ip++; c->rip = (u64) in.ip + (u64)(s64)(c->r[RCX] == 0 ? rel : 0); return; }
    case 0xE4: case 0xE5: case 0xE6: case 0xE7: case 0xEC: case 0xED: case 0xEE: case 0xEF:
    case 0xFA: case 0xFB: case 0xF4: gp_fault(c); return;
    case 0xF5: RF(c) ^= CF_; break;
    case 0xF8: RF(c) &= ~(u64)CF_; break;
    case 0xF9: RF(c) |= CF_; break;
    case 0xFC: RF(c) &= ~(u64)DF_; break;
    case 0xFD: RF(c) |= DF_; break;
    case 0xF6: case 0xF7: {
        int sz = op & 1 ? opsz : 1, ext = (*in.ip >> 3) & 7;
        modrm(c, &in, &m, ext < 2 ? (sz == 1 ? 1 : (opsz == 2 ? 2 : 4)) : 0);
        u64 v = rdm(c, &in, &m, sz), mk = mskz(sz);
        switch (ext) {
        case 0: case 1: { u64 imm = sz == 1 ? *in.ip++ : immz(&in, opsz); f_logic(c, v & imm, sz); break; }
        case 2: wrm(c, &in, &m, sz, ~v & mk); break;
        case 3: { u64 r = f_sub(c, 0, v, 0, sz); RF(c) &= ~(u64)CF_; if (v & mk) RF(c) |= CF_; wrm(c, &in, &m, sz, r); break; }
        case 4: case 5: {
            unsigned __int128 p; int ov;
            if (ext == 4) { p = (unsigned __int128)(getr(c, RAX, sz, 0)) * v; ov = (p >> (sz * 8)) != 0; }
            else { __int128 sp = (__int128) sx(getr(c, RAX, sz, 0), sz) * sx(v, sz); p = (unsigned __int128) sp; ov = sp != (__int128) sx((u64) sp & mk, sz); }
            if (sz == 1) setr(c, RAX, 2, 0, (u64) p);
            else { setr(c, RAX, sz, 0, (u64) p); setr(c, RDX, sz, 0, (u64)(p >> (sz * 8))); }
            RF(c) &= ~(u64)(CF_ | OF_); if (ov) RF(c) |= CF_ | OF_;
            break; }
        default: c->rip = (u64) in.ip; do_div(c, sz, v, ext == 7, start);
        }
        break; }
    case 0xFE: case 0xFF: {
        int sz = op & 1 ? opsz : 1, ext = (*in.ip >> 3) & 7;
        modrm(c, &in, &m, 0);
        if (ext < 2) {
            u64 v = rdm(c, &in, &m, sz), keep = RF(c) & CF_, r = ext == 0 ? f_add(c, v, 1, 0, sz) : f_sub(c, v, 1, 0, sz);
            RF(c) = (RF(c) & ~(u64)CF_) | keep; wrm(c, &in, &m, sz, r);
        } else if (op == 0xFF && (ext == 2 || ext == 4)) {
            u64 t = rdm(c, &in, &m, 8);
            if (ext == 2) push(c, (u64) in.ip);
            c->rip = t; return;
        } else if (op == 0xFF && ext == 6) { u64 v = rdm(c, &in, &m, 8); push(c, v); }
        else goto bad;
        break; }
    case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
        if (x87(c, &in, op) < 0) undef(c, start, "x87");
        break;
    case 0x0F: {
        u8 o2 = *in.ip++;
        switch (o2) {
        case 0x05: { c->rip = (u64) in.ip; c->r[RCX] = c->rip; c->r[11] = RF(c); do_syscall(c); return; }
        case 0x0B: goto bad;
        case 0x0D: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
            modrm(c, &in, &m, 0); break;
        case 0x31: { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); u64 t = (u64) ts.tv_sec * 1000000000ULL + (u64) ts.tv_nsec; c->r[RAX] = (u32) t; c->r[RDX] = t >> 32; break; }
        case 0xA2: cpuid(c); break;
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
        case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: {
            modrm(c, &in, &m, 0); u64 v = rdm(c, &in, &m, opsz);
            if (!cond(c, o2 & 15)) v = getr(c, m.reg, opsz, 0);
            setr(c, m.reg, opsz, 0, v); break; }
        case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
        case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F: {
            s32 rel = imm32(&in); c->rip = (u64) in.ip + (u64)(s64)(cond(c, o2 & 15) ? rel : 0); return; }
        case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
        case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F:
            modrm(c, &in, &m, 0); wrm(c, &in, &m, 1, cond(c, o2 & 15)); break;
        case 0xA3: case 0xAB: case 0xB3: case 0xBB: case 0xBA: {
            int imm = o2 == 0xBA, kind;
            modrm(c, &in, &m, imm ? 1 : 0);
            kind = imm ? (m.reg & 7) - 4 : o2 == 0xA3 ? 0 : o2 == 0xAB ? 1 : o2 == 0xB3 ? 2 : 3;
            if (kind < 0 || kind > 3) goto bad;
            int bits = opsz * 8, bit; u64 ea = m.ea, val;
            if (imm) { bit = *in.ip++ & (bits - 1); val = rdm(c, &in, &m, opsz); }
            else if (m.ismem) {
                s64 off = sx(getr(c, m.reg, opsz, in.rex), opsz);
                s64 q = off >= 0 ? off / bits : -((-off + bits - 1) / bits);
                ea = m.ea + (u64)(q * opsz); bit = (int)(off - q * bits); val = mrd(ea, opsz);
            } else { bit = (int)(getr(c, m.reg, opsz, in.rex) & (u64)(bits - 1)); val = rdm(c, &in, &m, opsz); }
            RF(c) &= ~(u64)CF_; if ((val >> bit) & 1) RF(c) |= CF_;
            if (kind) {
                val = kind == 1 ? val | (1ULL << bit) : kind == 2 ? val & ~(1ULL << bit) : val ^ (1ULL << bit);
                if (m.ismem) mwr(ea, val, opsz); else wrm(c, &in, &m, opsz, val);
            }
            break; }
        case 0xA4: case 0xA5: case 0xAC: case 0xAD: {
            modrm(c, &in, &m, (o2 & 1) ? 0 : 1);
            unsigned n = (o2 & 1) ? (unsigned)(c->r[RCX] & 0xff) : *in.ip++;
            n &= opsz == 8 ? 63 : 31;
            u64 dv = rdm(c, &in, &m, opsz), sr = getr(c, m.reg, opsz, in.rex);
            if (n && opsz >= 4) {
                int bits = opsz * 8; u64 r, mk = mskz(opsz); int cf;
                if (o2 < 0xA8) { r = ((dv << n) | (sr >> (bits - n))) & mk; cf = (int)((dv >> (bits - n)) & 1); }
                else { r = ((dv >> n) | (sr << (bits - n))) & mk; cf = (int)((dv >> (n - 1)) & 1); }
                RF(c) &= ~(u64)(CF_ | OF_); if (cf) RF(c) |= CF_;
                if (sgn(r, opsz) != sgn(dv, opsz)) RF(c) |= OF_;
                szp(c, r, opsz); wrm(c, &in, &m, opsz, r);
            }
            break; }
        case 0xAE: {
            modrm(c, &in, &m, 0);
            int ext = m.reg & 7;
            if (!m.ismem) { if (ext >= 5) break; goto bad; }
            if (ext == 0) { memset((void*) m.ea, 0, 512); *(u16u*) m.ea = c->fcw; *(u32u*)(m.ea + 24) = c->mxcsr; *(u32u*)(m.ea + 28) = 0xffff; memcpy((void*)(m.ea + 160), c->x, 256); }
            else if (ext == 1) { c->fcw = *(u16u*) m.ea; c->mxcsr = *(u32u*)(m.ea + 24); memcpy(c->x, (void*)(m.ea + 160), 256); }
            else if (ext == 2) c->mxcsr = *(u32u*) m.ea;
            else if (ext == 3) *(u32u*) m.ea = c->mxcsr;
            else if (ext == 7) { }
            else goto bad;
            break; }
        case 0xAF: {
            modrm(c, &in, &m, 0);
            s64 a = sx(getr(c, m.reg, opsz, in.rex), opsz), b = sx(rdm(c, &in, &m, opsz), opsz);
            __int128 p = (__int128) a * b; u64 r = (u64) p & mskz(opsz);
            RF(c) &= ~(u64)(CF_ | OF_); if (p != (__int128) sx(r, opsz)) RF(c) |= CF_ | OF_;
            setr(c, m.reg, opsz, in.rex, r); break; }
        case 0xB0: case 0xB1: {
            int sz = o2 & 1 ? opsz : 1; modrm(c, &in, &m, 0);
            u64 dv = rdm(c, &in, &m, sz), acc = getr(c, RAX, sz, 0);
            f_sub(c, acc, dv, 0, sz);
            if ((acc & mskz(sz)) == (dv & mskz(sz))) wrm(c, &in, &m, sz, getr(c, m.reg, sz, in.rex)); else setr(c, RAX, sz, 0, dv);
            break; }
        case 0xC0: case 0xC1: {
            int sz = o2 & 1 ? opsz : 1; modrm(c, &in, &m, 0);
            u64 dv = rdm(c, &in, &m, sz), sr = getr(c, m.reg, sz, in.rex), r = f_add(c, dv, sr, 0, sz);
            setr(c, m.reg, sz, in.rex, dv); wrm(c, &in, &m, sz, r); break; }
        case 0xB6: case 0xB7: case 0xBE: case 0xBF: {
            int ssz = (o2 & 1) ? 2 : 1; modrm(c, &in, &m, 0);
            u64 v = rdm(c, &in, &m, ssz);
            if (o2 >= 0xBE) v = (u64) sx(v, ssz);
            setr(c, m.reg, opsz, in.rex, v); break; }
        case 0xB8: if (!in.rep) goto bad; { modrm(c, &in, &m, 0); u64 v = rdm(c, &in, &m, opsz); u64 r = (u64) __builtin_popcountll(v & mskz(opsz));
            RF(c) &= ~(u64)(CF_ | OF_ | AF_ | SF_ | PF_ | ZF_); if (!v) RF(c) |= ZF_; setr(c, m.reg, opsz, in.rex, r); } break;
        case 0xBC: case 0xBD: {
            modrm(c, &in, &m, 0); u64 v = rdm(c, &in, &m, opsz) & mskz(opsz); int bits = opsz * 8;
            if (in.rep) {                                   /* tzcnt / lzcnt */
                u64 r = !v ? (u64) bits : o2 == 0xBC ? (u64) __builtin_ctzll(v) : (u64)(__builtin_clzll(v) - (64 - bits));
                RF(c) &= ~(u64)(CF_ | ZF_); if (!v) RF(c) |= CF_; if (!r) RF(c) |= ZF_;
                setr(c, m.reg, opsz, in.rex, r);
            } else if (!v) RF(c) |= ZF_;
            else { RF(c) &= ~(u64)ZF_; setr(c, m.reg, opsz, in.rex, o2 == 0xBC ? (u64) __builtin_ctzll(v) : (u64)(63 - __builtin_clzll(v))); }
            break; }
        case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF: {
            int r = (o2 & 7) | rexB;
            if (in.w) c->r[r] = __builtin_bswap64(c->r[r]); else c->r[r] = __builtin_bswap32((u32) c->r[r]);
            break; }
        default:
            if (sse(c, &in, o2) < 0) { undef(c, start, "0F/SSE"); }
        }
        break; }
    default:
    bad:
        undef(c, start, "opcode");
    }
    c->rip = (u64) in.ip;
}

/* ------------------------------------------------------------ JIT (x86_64 guest -> AArch64 host)
 *
 * Block-translating JIT, modelled on the one in pemu.c (the other direction). A block is a run of guest
 * instructions that ends at a branch, call, return, syscall or after JMAXI instructions.
 *
 *  - Guest registers stay in the CPU struct (x28 points at it), so every translated instruction loads its inputs
 *    from and stores its results to memory. That keeps the fallback trivial: an instruction the translator does not
 *    know is executed by the interpreter (step()) through a helper call, in the middle of a translated block, and
 *    guest state is always consistent at such a call.
 *  - Arithmetic flags stay lazy, exactly as in the interpreter (la/lb/lr/lmeta). When the next instruction of the
 *    block reads the flags (jcc, setcc, cmovcc) the host flags (NZCV) of the producing instruction are used directly
 *    (cmp + b.cond). The lazy fields are only written when the flags can still be observed (liveness within the
 *    block; flags are assumed live at a block exit) and always before anything that can
 *    fault or call a helper, so a fault or a signal sees exact flags.
 *  - Direct branches are chained (patched on first use); indirect ones use a direct-mapped inline cache. Pending
 *    signals are polled on backward and indirect branches and after every syscall.
 *  - Faults in translated code are mapped back to the guest rip through a host-pc -> guest-rip table.
 *  - The cache is flushed when the guest unmaps, re-protects or MAP_FIXED-overwrites pages that were translated.
 *    Pages that are writable and executable at the same time are never translated (x86 needs no explicit flush
 *    for self-modifying code), they are interpreted. The JIT turns itself off when a second guest thread appears.
 *
 * Register use of translated code:  x28 CPU*   x27 &pend   x26 inline cache
 *   x9,x10,x11  flag operands a,b,result of the last flag-producing instruction (kept for the lazy spill)
 *   x12,x13     effective address scratch      x0-x8,x14,x15  scratch       x16,x17  call / spill scratch
 *
 * With -DPEMU_JIT_EMBED (x86_64 test build) the generated AArch64 code is executed by the interpreter of pemu.c
 * (see tests/), which is how the back end is tested without an AArch64 machine. */
#ifdef HAVE_JIT

#ifdef PEMU_JIT_EMBED
extern void pemu_embed_run(u64 *x, u64 pc, u64 stop);
extern u64 pemu_embed_pc(void);
extern void pemu_embed_clear(void);
#endif

#define JCACHE_SZ  (64u << 20)          /* must stay below 128 MB: blocks branch to each other with B imm26 */
#define JTRAMP     64                   /* words reserved at the start of the cache (entry stub, epilogue) */
#define JH_BITS    18
#define JIC_BITS   13
#define JPG_BITS   15
#define JMAXI      48                   /* guest instructions per block */
#define JMAP_CAP   (4u << 20)
#define JRWX_MAX   64
#define JBLOCK_MAX 6144                 /* words a block may need at most; the cache is flushed when less is left */

#define RCPU  28
#define RPEND 27
#define RIC   26

#define OFF_R(n)   ((u32) offsetof(CPU, r) + 8u * (u32)(n))
#define OFF_RIP    ((u32) offsetof(CPU, rip))
#define OFF_FS     ((u32) offsetof(CPU, fs))
#define OFF_GS     ((u32) offsetof(CPU, gs))
#define OFF_LA     ((u32) offsetof(CPU, la))
#define OFF_LR     ((u32) offsetof(CPU, lr))
#define OFF_LMETA  ((u32) offsetof(CPU, lmeta))
#define OFF_PATCH  ((u32) offsetof(CPU, patch))

typedef struct { u64 pc; u32 *code; } JEnt;
static u32 *jbase, *jcur, *jlim, *jenter, *jepi;
static JEnt *jh, *jic;
static u32 jhn, jmapn, jpgn, jrwxn;
static u32 *jm_off; static u64 *jm_pc, *jpg;
static u64 jrwx[JRWX_MAX][2]; static int jrwx_over;
static volatile int g_jflush_req;
static unsigned long long jst_blocks, jst_flush, jst_chain, jst_ins, jst_words;
static int g_jstats;
static unsigned jfbh[512], jst_fb;                               /* statistics: interpreter fallbacks by opcode (0x100 + x for 0F xx) */
#ifdef PEMU_JIT_EMBED
static u64 jemb_stack[8192];
#endif

/* ------------------------------------------------------------ AArch64 encoders */
static inline void E(u32 w) { *jcur++ = w; }
#define SFB(sf) ((sf) ? 0x80000000u : 0u)

static void a_movz(int sf, int rd, u32 imm, int hw) { E(SFB(sf) | 0x52800000u | (u32) hw << 21 | (imm & 0xffff) << 5 | (u32) rd); }
static void a_movk(int sf, int rd, u32 imm, int hw) { E(SFB(sf) | 0x72800000u | (u32) hw << 21 | (imm & 0xffff) << 5 | (u32) rd); }
static void a_movn(int sf, int rd, u32 imm, int hw) { E(SFB(sf) | 0x12800000u | (u32) hw << 21 | (imm & 0xffff) << 5 | (u32) rd); }
static void a_movi(int rd, u64 v) {                       /* any 64-bit constant, 1-4 instructions, never touches the flags */
    int nz = 0, nzn = 0, first = 1;
    for (int h = 0; h < 4; h++) { nz += ((v >> (16 * h)) & 0xffff) != 0; nzn += ((~v >> (16 * h)) & 0xffff) != 0; }
    if (nzn < nz) {
        for (int h = 0; h < 4; h++) {
            u32 ch = (u32)(v >> (16 * h)) & 0xffff;
            if (ch == 0xffff) continue;
            if (first) { a_movn(1, rd, ~ch & 0xffff, h); first = 0; } else a_movk(1, rd, ch, h);
        }
        if (first) a_movn(1, rd, 0, 0);
    } else {
        for (int h = 0; h < 4; h++) {
            u32 ch = (u32)(v >> (16 * h)) & 0xffff;
            if (!ch) continue;
            if (first) { a_movz(1, rd, ch, h); first = 0; } else a_movk(1, rd, ch, h);
        }
        if (first) a_movz(1, rd, 0, 0);
    }
}
static void a_addsub(int sf, int sub, int s, int rd, int rn, int rm) {
    E(SFB(sf) | (sub ? 0x40000000u : 0) | (s ? 0x20000000u : 0) | 0x0B000000u | (u32) rm << 16 | (u32) rn << 5 | (u32) rd);
}
static void a_addsh(int rd, int rn, int rm, int lsl) {      /* add xd, xn, xm, lsl #n */
    E(0x8B000000u | (u32) rm << 16 | (u32) lsl << 10 | (u32) rn << 5 | (u32) rd);
}
static void a_addi(int sub, int rd, int rn, u32 imm12, int sh12) {    /* x-form add/sub immediate (rn, rd may be sp) */
    E(0x91000000u | (sub ? 0x40000000u : 0) | (u32) sh12 << 22 | (imm12 & 0xfff) << 10 | (u32) rn << 5 | (u32) rd);
}
static void a_log(int sf, int opc, int n, int rd, int rn, int rm) {    /* opc: 0 and, 1 orr, 2 eor, 3 ands; n: invert rm */
    E(SFB(sf) | (u32) opc << 29 | 0x0A000000u | (u32) n << 21 | (u32) rm << 16 | (u32) rn << 5 | (u32) rd);
}
static void a_logsh(int opc, int rd, int rn, int rm, int type, int amt) {   /* x-form with shifted rm */
    E(0x80000000u | (u32) opc << 29 | 0x0A000000u | (u32) type << 22 | (u32) rm << 16 | (u32) amt << 10 | (u32) rn << 5 | (u32) rd);
}
static void a_mov(int rd, int rm) { a_log(1, 1, 0, rd, 31, rm); }
static void a_movw(int rd, int rm) { a_log(0, 1, 0, rd, 31, rm); }      /* 32-bit move: zero-extends */
static void a_andmask(int rd, int rn, int nbits) { E(0x92400000u | (u32)(nbits - 1) << 10 | (u32) rn << 5 | (u32) rd); }   /* and xd, xn, #(2^nbits - 1) */
static void a_shv(int sf, int op, int rd, int rn, int rm) { E(SFB(sf) | 0x1AC02000u | (u32) op << 10 | (u32) rm << 16 | (u32) rn << 5 | (u32) rd); }   /* 0 lsl 1 lsr 2 asr 3 ror */
static void a_bfm(int sf, int sbfm, int rd, int rn, int immr, int imms) {
    E(SFB(sf) | (sbfm ? 0x13000000u : 0x53000000u) | (sf ? 0x400000u : 0) | (u32) immr << 16 | (u32) imms << 10 | (u32) rn << 5 | (u32) rd);
}
static void a_lsl(int sf, int rd, int rn, int n) { int b = sf ? 64 : 32; a_bfm(sf, 0, rd, rn, (b - n) & (b - 1), b - 1 - n); }
static void a_lsr(int sf, int rd, int rn, int n) { a_bfm(sf, 0, rd, rn, n, sf ? 63 : 31); }
static void a_asr(int sf, int rd, int rn, int n) { a_bfm(sf, 1, rd, rn, n, sf ? 63 : 31); }
static void a_sxth(int sf, int rd, int rn) { a_bfm(sf, 1, rd, rn, 0, 15); }
static void a_sxtw(int rd, int rn) { a_bfm(1, 1, rd, rn, 0, 31); }
static void a_mul(int sf, int rd, int rn, int rm) { E(SFB(sf) | 0x1B007C00u | (u32) rm << 16 | (u32) rn << 5 | (u32) rd); }
static void a_csel(int sf, int cc, int rd, int rn, int rm) { E(SFB(sf) | 0x1A800000u | (u32) rm << 16 | (u32) cc << 12 | (u32) rn << 5 | (u32) rd); }
static void a_cset(int rd, int cc) { E(0x1A9F07E0u | (u32)(cc ^ 1) << 12 | (u32) rd); }              /* cset wd, cc */
/* loads and stores: size 0 b, 1 h, 2 w, 3 x;  opc 0 store, 1 load, 2 load sign-extended to x, 3 load sign-extended to w */
static void a_ldst_u(int size, int opc, int rt, int rn, u32 off) { E((u32) size << 30 | 0x39000000u | (u32) opc << 22 | ((off >> size) & 0xfff) << 10 | (u32) rn << 5 | (u32) rt); }
static void a_ldst_s(int size, int opc, int rt, int rn, int off) { E((u32) size << 30 | 0x38000000u | (u32) opc << 22 | ((u32) off & 0x1ff) << 12 | (u32) rn << 5 | (u32) rt); }
static void a_stp(int rt, int rt2, int rn, int off) { E(0xA9000000u | ((u32)(off / 8) & 0x7f) << 15 | (u32) rt2 << 10 | (u32) rn << 5 | (u32) rt); }
static void a_ldp(int rt, int rt2, int rn, int off) { E(0xA9400000u | ((u32)(off / 8) & 0x7f) << 15 | (u32) rt2 << 10 | (u32) rn << 5 | (u32) rt); }
static void a_b(u32 *t) { E(0x14000000u | ((u32)(t - jcur) & 0x3ffffff)); }
static u32 *a_bcond_fwd(int cc) { E(0x54000000u | (u32) cc); return jcur - 1; }        /* patched with jpatch19 */
static u32 *a_cbz_fwd(int nz, int rt) { E((nz ? 0xB5000000u : 0xB4000000u) | (u32) rt); return jcur - 1; }
static void jpatch19(u32 *at) { *at |= ((u32)(jcur - at) & 0x7ffff) << 5; }
static void a_br(int rn) { E(0xD61F0000u | (u32) rn << 5); }
static void a_adr(int rd, u32 *target) { s64 d = (s64)((u8*) target - (u8*) jcur); E(0x10000000u | ((u32) d & 3) << 29 | (((u32)(d >> 2)) & 0x7ffff) << 5 | (u32) rd); }
static void j_callabs(void *f) {                                /* call a C helper; x0..x5 = arguments */
    a_movi(16, (u64) f);
#ifdef PEMU_JIT_EMBED
    E(0xD4000001u | 0x7001u << 5);                              /* svc #0x7001: host call (see pemu.c) */
#else
    E(0xD63F0000u | 16u << 5);                                  /* blr x16 */
#endif
}
static void j_sync_icache(void *a, void *b) {
#ifndef PEMU_JIT_EMBED
    __builtin___clear_cache((char*) a, (char*) b);
#else
    (void) a; (void) b;
#endif
}

/* ------------------------------------------------------------ guest state access */
static void c_ld(int rt, u32 off) { a_ldst_u(3, 1, rt, RCPU, off); }
static void c_st(int rt, u32 off) { a_ldst_u(3, 0, rt, RCPU, off); }
static void g_ld(int rt, int n, int sz, int rex) {              /* guest register n, zero-extended, into rt */
    u32 off = OFF_R(n);
    switch (sz) {
    case 8: a_ldst_u(3, 1, rt, RCPU, off); break;
    case 4: a_ldst_u(2, 1, rt, RCPU, off); break;
    case 2: a_ldst_u(1, 1, rt, RCPU, off); break;
    default: if (!rex && n >= 4 && n < 8) off = OFF_R(n - 4) + 1; a_ldst_u(0, 1, rt, RCPU, off);
    }
}
static void g_st(int rt, int n, int sz, int rex) {              /* sz 4 and 8 write the whole register: rt must hold a zero-extended value */
    u32 off = OFF_R(n);
    switch (sz) {
    case 8: case 4: a_ldst_u(3, 0, rt, RCPU, off); break;
    case 2: a_ldst_u(1, 0, rt, RCPU, off); break;
    default: if (!rex && n >= 4 && n < 8) off = OFF_R(n - 4) + 1; a_ldst_u(0, 0, rt, RCPU, off);
    }
}
static void m_ld(int sz, int sgn, int sf, int rt, int rn, int off) {      /* guest memory, [rn + off], off in -256..255 */
    int size = sz == 1 ? 0 : sz == 2 ? 1 : sz == 4 ? 2 : 3;
    a_ldst_s(size, !sgn ? 1 : (sf ? 2 : 3), rt, rn, off);
}
#define OFF_X(n) ((u32) offsetof(CPU, x) + 16u * (u32)(n))
static void q_ldc(int vt, u32 off) { E(0x3DC00000u | ((off >> 4) & 0xfff) << 10 | (u32) RCPU << 5 | (u32) vt); }       /* ldr qt, [cpu, #off] */
static void q_stc(int vt, u32 off) { E(0x3D800000u | ((off >> 4) & 0xfff) << 10 | (u32) RCPU << 5 | (u32) vt); }       /* str qt, [cpu, #off] */
static void q_ldm(int vt, int rn, int off) { E(0x3CC00000u | ((u32) off & 0x1ff) << 12 | (u32) rn << 5 | (u32) vt); }  /* ldur qt, [rn, #off] */
static void q_stm(int vt, int rn, int off) { E(0x3C800000u | ((u32) off & 0x1ff) << 12 | (u32) rn << 5 | (u32) vt); }  /* stur qt, [rn, #off] */
static void q_log(int op, int vd, int vn, int vm) {                      /* 0 eor 1 orr 2 and 3 bic: vd = vn op vm (16b) */
    static const u32 base[4] = { 0x6E201C00u, 0x4EA01C00u, 0x4E201C00u, 0x4E601C00u };
    E(base[op] | (u32) vm << 16 | (u32) vn << 5 | (u32) vd);
}
static void m_st(int sz, int rt, int rn, int off) {
    int size = sz == 1 ? 0 : sz == 2 ? 1 : sz == 4 ? 2 : 3;
    a_ldst_s(size, 0, rt, rn, off);
}

/* ------------------------------------------------------------ block cache */
static inline u32 hpc(u64 pc, int bits) { return (u32)((pc * 0x9E3779B97F4A7C15ULL) >> (64 - bits)); }
static inline u32 icidx(u64 pc) { return (u32)((pc ^ (pc >> 13)) & ((1u << JIC_BITS) - 1)); }
static u32 *jfind(u64 pc) {
    u32 h = hpc(pc, JH_BITS);
    for (;;) { if (jh[h].pc == pc) return jh[h].code; if (!jh[h].pc) return NULL; h = (h + 1) & ((1u << JH_BITS) - 1); }
}
static void jins(u64 pc, u32 *code) {
    u32 h = hpc(pc, JH_BITS);
    while (jh[h].pc) h = (h + 1) & ((1u << JH_BITS) - 1);
    jh[h].pc = pc; jh[h].code = code; jhn++;
}
static void jpg_add(u64 page) {
    u64 k = page | (1ULL << 63); u32 h = hpc(page << 12, JPG_BITS);
    for (;;) { if (jpg[h] == k) return; if (!jpg[h]) { jpg[h] = k; jpgn++; return; } h = (h + 1) & ((1u << JPG_BITS) - 1); }
}
static int jpg_has(u64 page) {
    u64 k = page | (1ULL << 63); u32 h = hpc(page << 12, JPG_BITS);
    for (;;) { if (jpg[h] == k) return 1; if (!jpg[h]) return 0; h = (h + 1) & ((1u << JPG_BITS) - 1); }
}
static int jit_has_code(u64 addr, u64 len) {
    if (!jpgn || !len) return 0;
    u64 p0 = addr >> 12, p1 = (addr + len - 1) >> 12;
    if (p1 < p0) p1 = ~0ULL >> 12;
    if (p1 - p0 < 64) { for (u64 p = p0; p <= p1; p++) if (jpg_has(p)) return 1; return 0; }
    for (u32 k = 0; k < (1u << JPG_BITS); k++) if (jpg[k]) { u64 p = jpg[k] & ~(1ULL << 63); if (p >= p0 && p <= p1) return 1; }
    return 0;
}
static void jflush(void) {
    jcur = jbase + JTRAMP;
    memset(jh, 0, sizeof(JEnt) << JH_BITS); memset(jic, 0, sizeof(JEnt) << JIC_BITS); memset(jpg, 0, sizeof(u64) << JPG_BITS);
    jhn = jmapn = jpgn = 0; g_cur->patch = 0; jst_flush++;
}
static int jit_is_rwx(u64 pc) {
    if (jrwx_over) return 1;
    u64 p = pc & ~4095ULL;
    for (u32 k = 0; k < jrwxn; k++) if (p < jrwx[k][1] && p + 4096 > jrwx[k][0]) return 1;
    return 0;
}
static void jit_note_rwx(u64 start, u64 len) {
    if (jrwxn < JRWX_MAX) { jrwx[jrwxn][0] = start; jrwx[jrwxn][1] = start + len; jrwxn++; } else jrwx_over = 1;
    if (jbase && jit_has_code(start, len)) g_jflush_req = 1;
}

/* ------------------------------------------------------------ decoder (static, no guest state) */
enum { OT_NONE, OT_REG, OT_MEM, OT_IMM };
enum { JO_FB, JO_FBT, JO_NOP, JO_MOV, JO_MOVX, JO_LEA, JO_PUSH, JO_POP, JO_ALU, JO_NOT, JO_NEG, JO_SHIFT, JO_IMUL,
       JO_SETCC, JO_CMOV, JO_CWDE, JO_CDQ, JO_LEAVE, JO_XLD, JO_XST, JO_XLOG, JO_XMOVDLD, JO_XMOVDST, JO_JMP, JO_JCC, JO_CALL, JO_RET, JO_JMPI, JO_CALLI };
typedef struct {
    u64 ip, imm, target; s64 disp;
    u8 len, op, sz, sz2, t, fr, fw, at, bt, ar, br, rex, lva, lvb, scale, seg, sx, cnt, nost;
    s8 base, index;
} JI;
typedef struct { int reg, rm, ismem; } JM;

static inline s32 rd32s(const u8*p) { s32 v; memcpy(&v, p, 4); return v; }
static inline s16 rd16s(const u8*p) { s16 v; memcpy(&v, p, 2); return v; }
static inline int jterm(const JI*j) { return j->op >= JO_JMP || j->op == JO_FBT; }

static void jmodrm(const u8 **pp, JI *j, JM *m, int immbytes) {      /* same semantics as modrm() but static */
    const u8 *p = *pp; u8 b = *p++; int rex = j->rex, ripr = 0, mod = b >> 6;
    m->reg = ((b >> 3) & 7) | ((rex & 4) << 1); m->rm = (b & 7) | ((rex & 1) << 3); m->ismem = mod != 3;
    j->base = j->index = -1; j->scale = 0; j->disp = 0;
    if (mod == 3) { *pp = p; return; }
    if ((b & 7) == 4) {
        u8 sib = *p++; int sc = sib >> 6, idx = ((sib >> 3) & 7) | ((rex & 2) << 2), base = (sib & 7) | ((rex & 1) << 3);
        if (idx != 4) { j->index = (s8) idx; j->scale = (u8) sc; }
        if ((sib & 7) == 5 && mod == 0) { j->disp = rd32s(p); p += 4; } else j->base = (s8) base;
    } else if ((b & 7) == 5 && mod == 0) { j->disp = rd32s(p); p += 4; ripr = 1; }
    else j->base = (s8) m->rm;
    if (mod == 1) { j->disp += (s8) *p; p += 1; } else if (mod == 2) { j->disp += rd32s(p); p += 4; }
    if (ripr) j->disp += (s64)(u64)(p + immbytes);                   /* rip-relative: absolute address */
    *pp = p;
}
static const u8 *jmodrm_skip(const u8 *p) {
    u8 b = *p++; int mod = b >> 6;
    if (mod == 3) return p;
    if ((b & 7) == 4) { u8 sib = *p++; if ((sib & 7) == 5 && mod == 0) p += 4; }
    else if ((b & 7) == 5 && mod == 0) p += 4;
    if (mod == 1) p += 1; else if (mod == 2) p += 4;
    return p;
}
static int jsse_ok(u8 o) {
    return (o >= 0x10 && o <= 0x17) || (o >= 0x28 && o <= 0x2F) || (o >= 0x50 && o <= 0x76) || (o >= 0x78 && o <= 0x7F) ||
           (o >= 0xC2 && o <= 0xC6) || o >= 0xD0;
}
/* End of an instruction that the translator does not handle itself. p = opcode byte. *neutral: touches no flags,
 * *term: ends the block. Returns NULL when the length is not known (the block then ends before the instruction). */
static const u8 *jfb_end(const u8 *p, int p66, int rexw, int *neutral, int *term) {
    u8 op = *p++; int zi = p66 ? 2 : 4;
    *neutral = 0; *term = 0;
    if (op < 0x40 && (op & 7) < 6) return (op & 7) < 4 ? jmodrm_skip(p) : p + ((op & 7) == 4 ? 1 : zi);
    switch (op) {
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
    case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: case 0x98: case 0x99:
    case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F:
    case 0xA4: case 0xA5: case 0xA6: case 0xA7: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
    case 0xC9: case 0xF5: case 0xF8: case 0xF9: case 0xFC: case 0xFD: return p;
    case 0x63: case 0x84: case 0x85: case 0x86: case 0x87: case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8D: case 0x8F:
    case 0xD0: case 0xD1: case 0xD2: case 0xD3: case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
    case 0xFE: return jmodrm_skip(p);
    case 0xFF: { int ext = (*p >> 3) & 7; if (ext == 2 || ext == 4) *term = 1; return jmodrm_skip(p); }
    case 0x68: return p + 4;
    case 0x6A: return p + 1;
    case 0x69: return jmodrm_skip(p) + zi;
    case 0x6B: case 0x80: case 0x83: case 0xC0: case 0xC1: case 0xC6: return jmodrm_skip(p) + 1;
    case 0x81: case 0xC7: return jmodrm_skip(p) + zi;
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: case 0xE3: case 0xEB: *term = 1; return p + 1;
    case 0xA8: return p + 1;
    case 0xA9: return p + zi;
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7: return p + 1;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF: return p + (rexw ? 8 : p66 ? 2 : 4);
    case 0xC2: *term = 1; return p + 2;
    case 0xC3: case 0xCC: case 0xE4: case 0xE5: case 0xE6: case 0xE7: case 0xEC: case 0xED: case 0xEE: case 0xEF:
    case 0xF4: case 0xFA: case 0xFB: *term = 1; return p;
    case 0xE8: case 0xE9: *term = 1; return p + 4;
    case 0xF6: case 0xF7: { int ext = (*p >> 3) & 7; const u8 *e = jmodrm_skip(p); if (ext < 2) e += op == 0xF6 ? 1 : zi; return e; }
    case 0x0F: {
        u8 o = *p++;
        switch (o) {
        case 0x05: case 0x0B: *term = 1; return p;
        case 0x31: case 0xA2: case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF: return p;
        case 0x0D: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F: *neutral = 1; return jmodrm_skip(p);
        case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
        case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F: *term = 1; return p + 4;
        case 0xBA: case 0xA4: case 0xAC: return jmodrm_skip(p) + 1;
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
        case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
        case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
        case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F:
        case 0xA3: case 0xAB: case 0xB3: case 0xBB: case 0xA5: case 0xAD: case 0xAE: case 0xAF: case 0xB0: case 0xB1:
        case 0xC0: case 0xC1: case 0xB6: case 0xB7: case 0xBE: case 0xBF: case 0xB8: case 0xBC: case 0xBD: return jmodrm_skip(p);
        default:
            if (!jsse_ok(o)) return NULL;
            *neutral = !(o == 0x2E || o == 0x2F);
            { const u8 *e = jmodrm_skip(p); if ((o >= 0x70 && o <= 0x73) || o == 0xC2 || o == 0xC4 || o == 0xC5 || o == 0xC6) e++; return e; }
        }
    }
    default: return NULL;
    }
}

/* Decode one instruction. Returns 0 if its length is unknown (the translator ends the block before it). */
static int jdecode(JI *j, u64 ip) {
    memset(j, 0, sizeof *j);
    const u8 *s = (const u8*) ip, *p = s;
    int p66 = 0, seg = 0, a32 = 0, lock = 0, pf3 = 0, pf2 = 0;
    for (int k = 0; k < 14; k++) {
        u8 b = *p;
        if (b == 0x66) p66 = 1; else if (b == 0xF3) pf3 = 1; else if (b == 0xF2) pf2 = 1; else if (b == 0x64) seg = 1; else if (b == 0x65) seg = 2;
        else if (b == 0x67) a32 = 1; else if (b == 0xF0) lock = 1; else if (b == 0x2E || b == 0x3E || b == 0x26 || b == 0x36) { } else break;
        p++;
    }
    int rex = 0; if ((*p & 0xF0) == 0x40) rex = *p++;
    int w = (rex >> 3) & 1, opsz = w ? 8 : p66 ? 2 : 4, rexB = (rex & 1) << 3;
    const u8 *op0 = p; u8 op = *p++;
    j->ip = ip; j->rex = (u8) rex; j->seg = (u8) seg;
#define FB() do { int nt_, tm_; const u8 *e_ = jfb_end(op0, p66, w, &nt_, &tm_); if (!e_ || e_ - s > 15) return 0; \
                  j->len = (u8)(e_ - s); j->op = tm_ ? JO_FBT : JO_FB; j->fr = j->fw = !nt_; return 1; } while (0)
#define BADM(m) (lock || (a32 && (m).ismem))
#define SETRM(m, which) do { if ((which) == 0) { j->at = (m).ismem ? OT_MEM : OT_REG; j->ar = (u8)(m).rm; } else { j->bt = (m).ismem ? OT_MEM : OT_REG; j->br = (u8)(m).rm; } } while (0)
#define IMMZ(dst) do { if (opsz == 2) { dst = (u64)(s64) rd16s(p); p += 2; } else { dst = (u64)(s64) rd32s(p); p += 4; } } while (0)
    JM m;
    if (op < 0x40 && (op & 7) < 6) {
        int t = op >> 3, f = op & 7;
        if (t == 2 || t == 3) FB();
        j->op = JO_ALU; j->t = (u8) t; j->nost = t == 7; j->fw = 1;
        if (f < 4) {
            j->sz = (f & 1) ? (u8) opsz : 1;
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            if (f < 2) { SETRM(m, 0); j->bt = OT_REG; j->br = (u8) m.reg; } else { j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); }
        } else {
            j->sz = f == 4 ? 1 : (u8) opsz; j->at = OT_REG; j->ar = RAX; j->bt = OT_IMM;
            if (j->sz == 1) j->imm = (u64)(s64)(s8) *p++; else IMMZ(j->imm);
        }
        j->len = (u8)(p - s); return 1;
    }
    switch (op) {
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
        j->op = JO_PUSH; j->sz = 8; j->at = OT_REG; j->ar = (u8)((op & 7) | rexB); break;
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        j->op = JO_POP; j->sz = 8; j->at = OT_REG; j->ar = (u8)((op & 7) | rexB); break;
    case 0x63:
        jmodrm(&p, j, &m, 0); if (BADM(m) || opsz == 2) FB();
        j->op = JO_MOVX; j->sz = (u8) opsz; j->sz2 = 4; j->sx = opsz == 8; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
    case 0x68: j->op = JO_PUSH; j->sz = 8; j->at = OT_IMM; j->imm = (u64)(s64) rd32s(p); p += 4; break;
    case 0x6A: j->op = JO_PUSH; j->sz = 8; j->at = OT_IMM; j->imm = (u64)(s64)(s8) *p++; break;
    case 0x69: case 0x6B:
        if (opsz == 2) FB();
        jmodrm(&p, j, &m, op == 0x69 ? 4 : 1); if (BADM(m)) FB();
        j->op = JO_IMUL; j->sz = (u8) opsz; j->sx = 1; j->fr = 1; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1);
        if (op == 0x69) { j->imm = (u64)(s64) rd32s(p); p += 4; } else j->imm = (u64)(s64)(s8) *p++;
        break;
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
        s8 rel = (s8) *p++; j->op = JO_JCC; j->t = op & 15; j->fr = 1; j->len = (u8)(p - s); j->target = ip + j->len + (u64)(s64) rel; return 1; }
    case 0x80: case 0x81: case 0x83: {
        j->sz = op == 0x80 ? 1 : (u8) opsz;
        jmodrm(&p, j, &m, op == 0x81 ? (opsz == 2 ? 2 : 4) : 1); if (BADM(m)) FB();
        int t = m.reg & 7; if (t == 2 || t == 3) FB();
        j->op = JO_ALU; j->t = (u8) t; j->nost = t == 7; j->fw = 1; SETRM(m, 0); j->bt = OT_IMM;
        if (op == 0x81) IMMZ(j->imm); else j->imm = (u64)(s64)(s8) *p++;
        break; }
    case 0x84: case 0x85:
        j->sz = (op & 1) ? (u8) opsz : 1; jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
        j->op = JO_ALU; j->t = 4; j->nost = 1; j->fw = 1; SETRM(m, 0); j->bt = OT_REG; j->br = (u8) m.reg; break;
    case 0x88: case 0x89: case 0x8A: case 0x8B:
        j->sz = (op & 1) ? (u8) opsz : 1; jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
        j->op = JO_MOV;
        if (op < 0x8A) { SETRM(m, 0); j->bt = OT_REG; j->br = (u8) m.reg; } else { j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); }
        break;
    case 0x8D:
        j->seg = 0; jmodrm(&p, j, &m, 0); if (BADM(m) || !m.ismem || opsz == 2) FB();
        j->op = JO_LEA; j->sz = (u8) opsz; j->at = OT_REG; j->ar = (u8) m.reg; break;
    case 0x90: if (rexB) FB(); j->op = JO_NOP; break;
    case 0x98: if (opsz == 2) FB(); j->op = JO_CWDE; j->sz = (u8) opsz; break;
    case 0x99: if (opsz == 2) FB(); j->op = JO_CDQ; j->sz = (u8) opsz; break;
    case 0xA8: j->op = JO_ALU; j->t = 4; j->nost = 1; j->fw = 1; j->sz = 1; j->at = OT_REG; j->ar = RAX; j->bt = OT_IMM; j->imm = (u8) *p++; break;
    case 0xA9: j->op = JO_ALU; j->t = 4; j->nost = 1; j->fw = 1; j->sz = (u8) opsz; j->at = OT_REG; j->ar = RAX; j->bt = OT_IMM; IMMZ(j->imm); break;
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        j->op = JO_MOV; j->sz = 1; j->at = OT_REG; j->ar = (u8)((op & 7) | rexB); j->bt = OT_IMM; j->imm = (u8) *p++; break;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        j->op = JO_MOV; j->at = OT_REG; j->ar = (u8)((op & 7) | rexB); j->bt = OT_IMM;
        if (w) { memcpy(&j->imm, p, 8); p += 8; j->sz = 8; }
        else if (p66) { u16 v; memcpy(&v, p, 2); j->imm = v; p += 2; j->sz = 2; }
        else { u32 v; memcpy(&v, p, 4); j->imm = v; p += 4; j->sz = 4; }
        break;
    case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
        int sz = (op & 1) ? opsz : 1;
        jmodrm(&p, j, &m, op <= 0xC1 ? 1 : 0); if (BADM(m)) FB();
        int t = m.reg & 7; if (t == 2 || t == 3 || sz < 4) FB();
        j->op = JO_SHIFT; j->sz = (u8) sz; j->t = (u8) t; SETRM(m, 0);
        j->cnt = op <= 0xC1 ? 0 : op <= 0xD1 ? 1 : 2;
        if (j->cnt == 0) j->imm = (u8) *p++ & (sz == 8 ? 63 : 31); else if (j->cnt == 1) j->imm = 1;
        if (j->cnt == 2 || !j->imm || t < 2) j->fr = 1; else j->fw = 1;
        break; }
    case 0xC2: j->op = JO_RET; j->imm = (u16)(*p | p[1] << 8); p += 2; break;
    case 0xC3: j->op = JO_RET; break;
    case 0xC6: case 0xC7: {
        j->sz = (op & 1) ? (u8) opsz : 1;
        jmodrm(&p, j, &m, j->sz == 1 ? 1 : (opsz == 2 ? 2 : 4)); if (BADM(m) || (m.reg & 7)) FB();
        j->op = JO_MOV; SETRM(m, 0); j->bt = OT_IMM;
        if (j->sz == 1) j->imm = (u8) *p++; else IMMZ(j->imm);
        break; }
    case 0xC9: j->op = JO_LEAVE; break;
    case 0xE8: case 0xE9: {
        s32 rel = rd32s(p); p += 4; j->len = (u8)(p - s);
        j->op = op == 0xE8 ? JO_CALL : JO_JMP; j->target = ip + j->len + (u64)(s64) rel; j->imm = ip + j->len; return 1; }
    case 0xEB: { s8 rel = (s8) *p++; j->len = (u8)(p - s); j->op = JO_JMP; j->target = ip + j->len + (u64)(s64) rel; return 1; }
    case 0xF6: case 0xF7: {
        int ext = (*p >> 3) & 7, sz = (op & 1) ? opsz : 1;
        jmodrm(&p, j, &m, ext < 2 ? (sz == 1 ? 1 : (opsz == 2 ? 2 : 4)) : 0); if (BADM(m) || ext > 3) FB();
        j->sz = (u8) sz; SETRM(m, 0);
        if (ext < 2) { j->op = JO_ALU; j->t = 4; j->nost = 1; j->fw = 1; j->bt = OT_IMM; if (sz == 1) j->imm = (u8) *p++; else IMMZ(j->imm); }
        else if (ext == 2) j->op = JO_NOT; else { j->op = JO_NEG; j->fw = 1; }
        break; }
    case 0xFF: {
        int ext = (*p >> 3) & 7;
        if (ext != 2 && ext != 4) FB();
        jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
        j->op = ext == 2 ? JO_CALLI : JO_JMPI; j->sz = 8; SETRM(m, 0); j->len = (u8)(p - s); j->imm = ip + j->len; return 1; }
    case 0x0F: {
        u8 o2 = *p++;
        switch (o2) {
        case 0x0D: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
            p = jmodrm_skip(p); j->op = JO_NOP; break;
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
        case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
            jmodrm(&p, j, &m, 0); if (BADM(m) || opsz == 2) FB();
            j->op = JO_CMOV; j->t = o2 & 15; j->sz = (u8) opsz; j->fr = 1; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
        case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F: {
            s32 rel = rd32s(p); p += 4; j->len = (u8)(p - s); j->op = JO_JCC; j->t = o2 & 15; j->fr = 1; j->target = ip + j->len + (u64)(s64) rel; return 1; }
        case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
        case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F:
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->op = JO_SETCC; j->t = o2 & 15; j->sz = 1; j->fr = 1; SETRM(m, 0); break;
        case 0xAF:
            jmodrm(&p, j, &m, 0); if (BADM(m) || opsz == 2) FB();
            j->op = JO_IMUL; j->sz = (u8) opsz; j->fr = 1; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0xB6: case 0xB7: case 0xBE: case 0xBF:
            jmodrm(&p, j, &m, 0); if (BADM(m) || opsz == 2) FB();
            j->op = JO_MOVX; j->sz = (u8) opsz; j->sz2 = (o2 & 1) ? 2 : 1; j->sx = o2 >= 0xBE; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0x10: case 0x28: case 0x6F: case 0x11: case 0x29: case 0x7F: {
            int pf = pf3 ? 3 : pf2 ? 2 : p66 ? 1 : 0, st = o2 == 0x11 || o2 == 0x29 || o2 == 0x7F;
            if ((o2 == 0x10 || o2 == 0x11) ? pf > 1 : (o2 == 0x6F || o2 == 0x7F) ? pf == 0 : 0) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->op = st ? JO_XST : JO_XLD;
            if (st) { SETRM(m, 0); j->bt = OT_REG; j->br = (u8) m.reg; } else { j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); }
            break; }
        case 0xEF: case 0xEB: case 0xDB: case 0xDF:
            if (!(p66 && !pf3 && !pf2)) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->op = JO_XLOG; j->t = o2 == 0xEF ? 0 : o2 == 0xEB ? 1 : o2 == 0xDB ? 2 : 3;
            j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0x6E: case 0x7E:
            if (!(p66 && !pf3 && !pf2)) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->sz = w ? 8 : 4;
            if (o2 == 0x6E) { j->op = JO_XMOVDLD; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); }
            else { j->op = JO_XMOVDST; SETRM(m, 0); j->bt = OT_REG; j->br = (u8) m.reg; }
            break;
        default: FB();
        }
        break; }
    default: FB();
    }
    j->len = (u8)(p - s);
    return 1;
}

/* ------------------------------------------------------------ flag state and code generation */
static struct { int nz, pend, kind, sz; } fl;        /* nz: host NZCV and x9-x11 hold the flags of kind/sz; pend: lazy fields not written yet */
static void jh_step(CPU *c) { step(c); }

static void fl_spill(void) {
    if (fl.kind == LZ_LOGIC) a_stp(31, 31, RCPU, (int) OFF_LA); else a_stp(9, 10, RCPU, (int) OFF_LA);
    c_st(11, OFF_LR);
    a_movi(17, (u64) fl.kind | (u64) fl.sz << 8);
    c_st(17, OFF_LMETA);
}
static void fl_flush(int live) { if (fl.pend) { if (live) fl_spill(); fl.pend = 0; } }

/* x86 condition code -> A64 condition for the host flags left by the last producer.
 * returns 0 normal (*cc set), 1 always true, 2 always false, -1 not expressible */
static int jmapcc(int x86cc, int *cc) {
    static const s8 sub_t[16] = { 6, 7, 3, 2, 0, 1, 9, 8, 4, 5, -1, -1, 11, 10, 13, 12 };
    static const s8 add_t[16] = { 6, 7, 2, 3, 0, 1, -1, -1, 4, 5, -1, -1, 11, 10, 13, 12 };
    static const s8 log_t[16] = { -2, -3, -2, -3, 0, 1, 0, 1, 4, 5, -1, -1, 11, 10, 13, 12 };
    int v = fl.kind == LZ_SUB ? sub_t[x86cc] : fl.kind == LZ_ADD ? add_t[x86cc] : log_t[x86cc];
    if (v == -2) return 2;
    if (v == -3) return 1;
    if (v < 0) return -1;
    *cc = v; return 0;
}

static void j_ea(const JI *j, int *rb, int *roff) {   /* address = x[*rb] + *roff, *roff in -256..255; uses x12, x13 */
    s64 d = j->disp;
    if (j->base < 0 && j->index < 0) {
        a_movi(12, (u64) d);
        if (j->seg) { c_ld(13, j->seg == 1 ? OFF_FS : OFF_GS); a_addsh(12, 12, 13, 0); }
        *rb = 12; *roff = 0; return;
    }
    int have = 0;
    if (j->base >= 0) { c_ld(12, OFF_R(j->base)); have = 1; }
    if (j->index >= 0) {
        c_ld(13, OFF_R(j->index));
        if (have) a_addsh(12, 12, 13, j->scale); else a_lsl(1, 12, 13, j->scale);
    }
    if (j->seg) { c_ld(13, j->seg == 1 ? OFF_FS : OFF_GS); a_addsh(12, 12, 13, 0); }
    if (d >= -256 && d <= 255) { *rb = 12; *roff = (int) d; return; }
    if (d > 0 && d < (1 << 24)) { if (d & 0xfff) a_addi(0, 12, 12, (u32) d & 0xfff, 0); if (d >> 12) a_addi(0, 12, 12, (u32)(d >> 12), 1); }
    else if (d < 0 && d > -(1 << 24)) { u32 mm = (u32) -d; if (mm & 0xfff) a_addi(1, 12, 12, mm & 0xfff, 0); if (mm >> 12) a_addi(1, 12, 12, mm >> 12, 1); }
    else { a_movi(13, (u64) d); a_addsh(12, 12, 13, 0); }
    *rb = 12; *roff = 0;
}
static inline u64 szmask(int sz) { return sz == 8 ? ~0ULL : (1ULL << (sz * 8)) - 1; }
static void j_opld(int rt, const JI *j, int which, int sz, int rb, int off) {
    int ot = which ? j->bt : j->at, r = which ? j->br : j->ar;
    if (ot == OT_REG) g_ld(rt, r, sz, j->rex);
    else if (ot == OT_MEM) m_ld(sz, 0, 0, rt, rb, off);
    else a_movi(rt, j->imm & szmask(sz));
}
static void j_opst(int rt, const JI *j, int sz, int rb, int off) {
    if (j->at == OT_REG) g_st(rt, j->ar, sz, j->rex); else m_st(sz, rt, rb, off);
}
static void j_push(int rv) { c_ld(1, OFF_R(RSP)); a_addi(1, 1, 1, 8, 0); a_ldst_s(3, 0, rv, 1, 0); c_st(1, OFF_R(RSP)); }
static void j_pop(int rv) { c_ld(1, OFF_R(RSP)); a_ldst_s(3, 1, rv, 1, 0); a_addi(0, 1, 1, 8, 0); c_st(1, OFF_R(RSP)); }

static void j_fb(const JI *j) {                       /* run one instruction through the interpreter */
    a_movi(0, j->ip); c_st(0, OFF_RIP); a_mov(0, RCPU);
    j_callabs((void*) jh_step);
    fl.nz = 0;
}

/* ---- block exits */
static void jx_direct(u64 from, u64 target) {
    if (target <= from) {                              /* backward branch: poll for pending signals */
        a_ldst_u(3, 1, 0, RPEND, 0);
        u32 *z = a_cbz_fwd(0, 0);
        a_movi(0, target); c_st(0, OFF_RIP); a_b(jepi);
        jpatch19(z);
    }
    u32 *tc = jfind(target);
    if (tc) { a_b(tc); return; }
    u32 *site = jcur; E(0x14000001u);                  /* b .+4: patched to "b target" once the target is translated */
    a_adr(0, site); c_st(0, OFF_PATCH);
    a_movi(0, target); c_st(0, OFF_RIP); a_b(jepi);
}
static void jx_indirect(void) {                        /* guest target in x0 */
    a_ldst_u(3, 1, 1, RPEND, 0);
    u32 *s1 = a_cbz_fwd(1, 1);
    a_logsh(2, 2, 0, 0, 1, 13);                        /* eor x2, x0, x0, lsr #13 */
    a_andmask(2, 2, JIC_BITS);
    a_addsh(2, RIC, 2, 4);
    a_ldp(3, 4, 2, 0);
    a_addsub(1, 1, 1, 31, 3, 0);                       /* cmp x3, x0 */
    u32 *s2 = a_bcond_fwd(1);                          /* b.ne */
    a_br(4);
    jpatch19(s1); jpatch19(s2);
    c_st(0, OFF_RIP); a_b(jepi);
}

/* ---- arithmetic: x9 = a, x10 = b (zero-extended to size) -> x11 = result; host flags + x9-x11 are the lazy state when nf */
static void alu_core(int t, int sz, int nf) {
    int sf = sz == 8, sh = sz == 1 ? 24 : sz == 2 ? 16 : 0;
    if (!nf) {
        switch (t) {
        case 0: a_addsub(sf, 0, 0, 11, 9, 10); break;
        case 1: a_log(sf, 1, 0, 11, 9, 10); break;
        case 4: a_log(sf, 0, 0, 11, 9, 10); break;
        case 6: a_log(sf, 2, 0, 11, 9, 10); break;
        default: a_addsub(sf, 1, 0, 11, 9, 10); break;      /* sub, cmp */
        }
        return;
    }
    switch (t) {
    case 0: case 5: case 7: {
        int sub = t != 0;
        if (sh) { a_lsl(0, 13, 9, sh); a_lsl(0, 14, 10, sh); a_addsub(0, sub, 1, 15, 13, 14); a_lsr(0, 11, 15, sh); }
        else a_addsub(sf, sub, 1, 11, 9, 10);
        fl.kind = sub ? LZ_SUB : LZ_ADD; break; }
    case 4:
        if (sh) { a_log(0, 0, 0, 11, 9, 10); a_lsl(0, 15, 11, sh); a_log(0, 3, 0, 31, 15, 15); } else a_log(sf, 3, 0, 11, 9, 10);
        fl.kind = LZ_LOGIC; break;
    default:
        a_log(sf, t == 1 ? 1 : 2, 0, 11, 9, 10);
        if (sh) { a_lsl(0, 15, 11, sh); a_log(0, 3, 0, 31, 15, 15); } else a_log(sf, 3, 0, 31, 11, 11);
        fl.kind = LZ_LOGIC; break;
    }
    fl.sz = sz; fl.nz = 1; fl.pend = 1;
}

static void jemit(const JI *j) {
    int sz = j->sz, rb = 0, off = 0, hm = j->at == OT_MEM || j->bt == OT_MEM, sf = sz == 8;
    switch (j->op) {
    case JO_NOP: break;
    case JO_FB: fl_flush(j->lvb); j_fb(j); break;
    case JO_FBT: fl_flush(1); j_fb(j); a_b(jepi); break;
    case JO_MOV:
        if (hm) { fl_flush(j->lvb); j_ea(j, &rb, &off); }
        j_opld(0, j, 1, sz, rb, off); j_opst(0, j, sz, rb, off); break;
    case JO_MOVX: {
        int size = j->sz2 == 1 ? 0 : j->sz2 == 2 ? 1 : 2, opc = !j->sx ? 1 : (j->sz == 8 ? 2 : 3);
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); m_ld(j->sz2, j->sx, j->sz == 8, 0, rb, off); }
        else {
            u32 o = OFF_R(j->br);
            if (j->sz2 == 1 && !j->rex && j->br >= 4 && j->br < 8) o = OFF_R(j->br - 4) + 1;
            a_ldst_u(size, opc, 0, RCPU, o);
        }
        g_st(0, j->ar, j->sz, j->rex); break; }
    case JO_LEA: {
        j_ea(j, &rb, &off);
        if (off >= 0) a_addi(0, 0, rb, (u32) off, 0); else a_addi(1, 0, rb, (u32) -off, 0);
        if (j->sz == 4) a_movw(0, 0);
        g_st(0, j->ar, j->sz, j->rex); break; }
    case JO_PUSH:
        fl_flush(j->lvb);
        if (j->at == OT_REG) g_ld(0, j->ar, 8, 0); else a_movi(0, j->imm);
        j_push(0); break;
    case JO_POP: fl_flush(j->lvb); j_pop(0); g_st(0, j->ar, 8, 0); break;
    case JO_LEAVE:
        fl_flush(j->lvb);
        c_ld(0, OFF_R(RBP)); a_ldst_s(3, 1, 2, 0, 0); a_addi(0, 0, 0, 8, 0); c_st(0, OFF_R(RSP)); c_st(2, OFF_R(RBP)); break;
    case JO_CWDE:
        if (sz == 8) { g_ld(0, RAX, 4, 0); a_sxtw(0, 0); g_st(0, RAX, 8, 0); }
        else { g_ld(0, RAX, 2, 0); a_sxth(0, 0, 0); g_st(0, RAX, 4, 0); }
        break;
    case JO_CDQ:
        if (sz == 8) { g_ld(0, RAX, 8, 0); a_asr(1, 0, 0, 63); g_st(0, RDX, 8, 0); }
        else { g_ld(0, RAX, 4, 0); a_asr(0, 0, 0, 31); g_st(0, RDX, 4, 0); }
        break;
    case JO_ALU: {
        int nf = j->lva;
        fl_flush(j->lvb); fl.nz = 0;
        if (hm) j_ea(j, &rb, &off);
        j_opld(9, j, 0, sz, rb, off); j_opld(10, j, 1, sz, rb, off);
        alu_core(j->t, sz, nf);
        if (!j->nost) j_opst(11, j, sz, rb, off);
        break; }
    case JO_NEG: {
        int nf = j->lva;
        fl_flush(j->lvb); fl.nz = 0;
        if (j->at == OT_MEM) j_ea(j, &rb, &off);
        j_opld(10, j, 0, sz, rb, off); a_movz(0, 9, 0, 0);
        alu_core(5, sz, nf);
        j_opst(11, j, sz, rb, off); break; }
    case JO_NOT:
        if (j->at == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); }
        j_opld(0, j, 0, sz, rb, off); a_log(sf, 1, 1, 0, 31, 0); j_opst(0, j, sz, rb, off); break;
    case JO_SHIFT: {
        if (j->lva) { fl_flush(j->lvb); j_fb(j); break; }        /* flags wanted: interpreter */
        fl_flush(j->lvb); fl.nz = 0;
        if (j->at == OT_MEM) j_ea(j, &rb, &off);
        j_opld(0, j, 0, sz, rb, off);
        if (j->cnt == 2) {
            c_ld(1, OFF_R(RCX));
            switch (j->t) {
            case 4: case 6: a_shv(sf, 0, 0, 0, 1); break;
            case 5: a_shv(sf, 1, 0, 0, 1); break;
            case 7: a_shv(sf, 2, 0, 0, 1); break;
            case 1: a_shv(sf, 3, 0, 0, 1); break;
            default: a_addsub(1, 1, 0, 1, 31, 1); a_shv(sf, 3, 0, 0, 1); break;     /* rol: ror by -cl */
            }
        } else if (j->imm) {
            int n = (int) j->imm;
            switch (j->t) {
            case 4: case 6: a_lsl(sf, 0, 0, n); break;
            case 5: a_lsr(sf, 0, 0, n); break;
            case 7: a_asr(sf, 0, 0, n); break;
            case 1: a_movi(1, (u64) n); a_shv(sf, 3, 0, 0, 1); break;
            default: a_movi(1, (u64)((sf ? 64 : 32) - n)); a_shv(sf, 3, 0, 0, 1); break;
            }
        }
        j_opst(0, j, sz, rb, off); break; }
    case JO_IMUL: {
        if (j->lva) { fl_flush(j->lvb); j_fb(j); break; }
        fl_flush(j->lvb); fl.nz = 0;
        if (j->bt == OT_MEM) j_ea(j, &rb, &off);
        j_opld(0, j, 1, sz, rb, off);
        if (j->sx) a_movi(1, j->imm); else g_ld(1, j->ar, sz, 0);
        a_mul(sf, 0, 0, 1); g_st(0, j->ar, sz, 0); break; }
    case JO_SETCC: {
        fl_flush(j->lvb);
        int cc = 0, m = fl.nz ? jmapcc(j->t, &cc) : -1;
        if (m < 0) { j_fb(j); break; }
        if (m == 0) a_cset(0, cc); else a_movz(0, 0, m == 1, 0);
        if (j->at == OT_MEM) { j_ea(j, &rb, &off); m_st(1, 0, rb, off); } else g_st(0, j->ar, 1, j->rex);
        break; }
    case JO_CMOV: {
        fl_flush(j->lvb);
        int cc = 0, m = fl.nz ? jmapcc(j->t, &cc) : -1;
        if (m < 0) { j_fb(j); break; }
        if (j->bt == OT_MEM) { j_ea(j, &rb, &off); m_ld(sz, 0, 0, 1, rb, off); } else g_ld(1, j->br, sz, 0);
        g_ld(0, j->ar, sz, 0);
        if (m == 0) a_csel(sf, cc, 0, 1, 0); else if (m == 1) a_mov(0, 1);
        g_st(0, j->ar, sz, 0); break; }
    case JO_XLD:
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); q_ldm(0, rb, off); } else q_ldc(0, OFF_X(j->br));
        q_stc(0, OFF_X(j->ar)); break;
    case JO_XST:
        q_ldc(0, OFF_X(j->br));
        if (j->at == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); q_stm(0, rb, off); } else q_stc(0, OFF_X(j->ar));
        break;
    case JO_XLOG:
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); q_ldm(1, rb, off); } else q_ldc(1, OFF_X(j->br));
        q_ldc(0, OFF_X(j->ar));
        if (j->t == 3) q_log(3, 0, 1, 0); else q_log(j->t, 0, 0, 1);       /* pandn: ~dst & src */
        q_stc(0, OFF_X(j->ar)); break;
    case JO_XMOVDLD:
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); m_ld(sz, 0, 0, 0, rb, off); } else g_ld(0, j->br, sz, 0);
        c_st(0, OFF_X(j->ar)); c_st(31, OFF_X(j->ar) + 8); break;
    case JO_XMOVDST:
        c_ld(0, OFF_X(j->br));
        if (j->at == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); m_st(sz, 0, rb, off); }
        else { if (sz == 4) a_movw(0, 0); g_st(0, j->ar, sz, 0); }
        break;
    case JO_JMP: fl_flush(j->lvb); jx_direct(j->ip, j->target); break;
    case JO_JCC: {
        fl_flush(1);
        int cc = 0, m = fl.nz ? jmapcc(j->t, &cc) : -1;
        u64 next = j->ip + j->len;
        if (m < 0) { j_fb(j); c_ld(0, OFF_RIP); jx_indirect(); }
        else if (m == 1) jx_direct(j->ip, j->target);
        else if (m == 2) jx_direct(j->ip, next);
        else { u32 *bt = a_bcond_fwd(cc); jx_direct(j->ip, next); jpatch19(bt); jx_direct(j->ip, j->target); }
        break; }
    case JO_CALL: fl_flush(j->lvb); a_movi(0, j->imm); j_push(0); jx_direct(j->ip, j->target); break;
    case JO_RET:
        fl_flush(j->lvb);
        c_ld(1, OFF_R(RSP)); a_ldst_s(3, 1, 0, 1, 0); a_addi(0, 1, 1, 8, 0);
        if (j->imm) { a_movi(2, j->imm); a_addsh(1, 1, 2, 0); }
        c_st(1, OFF_R(RSP)); jx_indirect(); break;
    case JO_JMPI: case JO_CALLI:
        fl_flush(j->lvb);
        if (j->at == OT_MEM) { j_ea(j, &rb, &off); m_ld(8, 0, 0, 0, rb, off); } else g_ld(0, j->ar, 8, 0);
        if (j->op == JO_CALLI) { a_movi(2, j->imm); j_push(2); }
        jx_indirect(); break;
    }
}

/* ------------------------------------------------------------ translation, dispatch, hooks */
static u32 *jtranslate(u64 pc) {
    if ((u64)(jlim - jcur) < 2 * JBLOCK_MAX || jmapn + 4 * JMAXI > JMAP_CAP || jhn * 2 > (1u << JH_BITS)) jflush();
    JI ji[JMAXI]; int n = 0; u64 ip = pc;
    while (n < JMAXI) {
        if (n && (ip & 0xfff) > 4096 - 16) break;               /* an instruction may cross into a page we have not looked at */
        JI *j = &ji[n];
        if (!jdecode(j, ip)) { memset(j, 0, sizeof *j); j->ip = ip; j->op = JO_FBT; j->fr = j->fw = 1; n++; break; }
        n++; ip += j->len;
        if (jterm(j)) break;
    }
    if (g_jstats)
        for (int i = 0; i < n; i++)
            if (ji[i].op == JO_FB || ji[i].op == JO_FBT) {
                const u8 *q = (const u8*) ji[i].ip;
                while (*q == 0x66 || *q == 0xF3 || *q == 0xF2 || *q == 0x64 || *q == 0x65 || *q == 0x67 || *q == 0xF0 || (*q & 0xE7) == 0x26) q++;
                if ((*q & 0xF0) == 0x40) q++;
                jfbh[*q == 0x0F ? 256 + q[1] : *q]++; jst_fb++;
            }
    for (int i = 0; i < n; i++) {                                 /* pages that now hold translated code */
        jpg_add(ji[i].ip >> 12);
        if (ji[i].len) jpg_add((ji[i].ip + ji[i].len - 1) >> 12);
    }
    /* flag liveness, backwards. Flags are assumed live when the block ends. With -DPEMU_JIT_ABI_FLAGS they are assumed dead
     * across call/ret (the SysV ABI does not preserve them): faster, but wrong for assembly that returns a result in the flags. */
#ifdef PEMU_JIT_ABI_FLAGS
    int live = (ji[n - 1].op == JO_CALL || ji[n - 1].op == JO_RET || ji[n - 1].op == JO_CALLI) ? 0 : 1;
#else
    int live = 1;
#endif
    for (int i = n - 1; i >= 0; i--) {
        ji[i].lva = (u8) live;
        if (ji[i].fr) live = 1; else if (ji[i].fw) live = 0;
        ji[i].lvb = (u8) live;
    }
    u32 *start = jcur;
    jins(pc, start);
    memset(&fl, 0, sizeof fl);
    for (int i = 0; i < n; i++) {
        jm_off[jmapn] = (u32)((u8*) jcur - (u8*) jbase); jm_pc[jmapn++] = ji[i].ip;
        jemit(&ji[i]);
    }
    if (!jterm(&ji[n - 1])) { fl_flush(1); jx_direct(ji[n - 1].ip, ji[n - 1].ip + ji[n - 1].len); }
    jst_blocks++; jst_ins += (unsigned) n; jst_words += (unsigned long long)(jcur - start);
    j_sync_icache(start, jcur);
    return start;
}

#define JSTOP 0x1000ULL
static void jit_enter(CPU *c, u32 *code) {
#ifdef PEMU_JIT_EMBED
    u64 x[32] = {0};
    x[0] = (u64) c; x[1] = (u64) code; x[2] = (u64)(void*) &pend; x[3] = (u64) jic; x[30] = JSTOP; x[31] = (u64)(jemb_stack + 8192);
    pemu_embed_run(x, (u64) jenter, JSTOP);
#else
    ((void (*)(CPU*, u32*, volatile u64*, JEnt*)) (void*) jenter)(c, code, &pend, jic);
#endif
}

static void jit_dispatch(CPU *c) {
    if (g_jflush_req) { g_jflush_req = 0; jflush(); }
    u64 pc = c->rip;
    u32 *code = jfind(pc);
    if (!code) {
        if ((pc & 0xfff) > 4096 - 16 || jit_is_rwx(pc)) { c->patch = 0; step(c); return; }     /* interpret: see header comment */
        code = jtranslate(pc);
    }
    if (c->patch) { u32 *site = (u32*) c->patch; *site = 0x14000000u | ((u32)(code - site) & 0x3ffffff); j_sync_icache(site, site + 1); c->patch = 0; jst_chain++; }
    jic[icidx(pc)].pc = pc; jic[icidx(pc)].code = code;
    jit_enter(c, code);
}

static u64 jit_pc2rip(void *u) {
    u64 pc;
#ifdef PEMU_JIT_EMBED
    (void) u; pc = pemu_embed_pc(); pemu_embed_clear();
#else
    pc = (u64) ((ucontext_t*) u)->uc_mcontext.pc;
#endif
    if (!pc || !jbase || pc < (u64) jbase || pc >= (u64) jcur || !jmapn) return 0;
    u32 off = (u32)(pc - (u64) jbase), lo = 0, hi = jmapn;
    while (hi - lo > 1) { u32 mid = (lo + hi) / 2; if (jm_off[mid] <= off) lo = mid; else hi = mid; }
    return jm_off[lo] <= off ? jm_pc[lo] : 0;
}

static void jit_post_syscall(CPU *c, u64 n, long r) {
    u64 a0 = c->r[RDI], a1 = c->r[RSI], a2 = c->r[RDX], a3 = c->r[10];
    switch (n) {
    case 9:                                                       /* mmap */
        if ((u64) r >= 0xfffffffffffff000ULL) break;
        if ((a2 & PROT_EXEC) && (a2 & PROT_WRITE)) jit_note_rwx((u64) r, a1);
        if ((a3 & MAP_FIXED) && jit_has_code(a0, a1)) g_jflush_req = 1;
        break;
    case 10:                                                      /* mprotect */
        if (r) break;
        if ((a2 & PROT_EXEC) && (a2 & PROT_WRITE)) jit_note_rwx(a0, a1);
        if (jit_has_code(a0, a1)) g_jflush_req = 1;
        break;
    case 11: if (!r && jit_has_code(a0, a1)) g_jflush_req = 1; break;        /* munmap */
    case 25: if ((u64) r < 0xfffffffffffff000ULL && jit_has_code(a0, a1)) g_jflush_req = 1; break;   /* mremap */
    default: break;
    }
}
static void jit_disable(void) { g_jit_on = 0; }
static void jit_report(void) {
    if (!g_jstats) return;
    g_jstats = 0;
    fprintf(stderr, "pemu jit: %llu blocks, %llu guest instructions translated (%.1f AArch64 instructions each), %llu chained exits, %llu cache flushes\n",
            jst_blocks, jst_ins, jst_ins ? (double) jst_words / (double) jst_ins : 0.0, jst_chain, jst_flush);
    fprintf(stderr, "pemu jit: %u of those run through the interpreter; most frequent opcodes:", jst_fb);
    for (int k = 0; k < 8; k++) {
        int b = -1; for (int i = 0; i < 512; i++) if (jfbh[i] && (b < 0 || jfbh[i] > jfbh[b])) b = i;
        if (b < 0) break;
        fprintf(stderr, " %s%02x x%u", b >= 256 ? "0f " : "", b & 255, jfbh[b]); jfbh[b] = 0;
    }
    fprintf(stderr, "\n");
}

static void *jmap(size_t n) {
    void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}
static int jit_init(void) {
    void *m = mmap(NULL, JCACHE_SZ, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (m == MAP_FAILED) return -1;                               /* no executable memory: stay with the interpreter */
    jbase = m; jlim = jbase + JCACHE_SZ / 4;
    jh = jmap(sizeof(JEnt) << JH_BITS); jic = jmap(sizeof(JEnt) << JIC_BITS); jpg = jmap(sizeof(u64) << JPG_BITS);
    jm_off = jmap(4ul * JMAP_CAP); jm_pc = jmap(8ul * JMAP_CAP);
    if (!jh || !jic || !jpg || !jm_off || !jm_pc) return -1;
    jcur = jbase;
    jenter = jcur;                                                /* void jenter(CPU *c, void *code, volatile u64 *pend, JEnt *ic) */
    E(0xA9800000u | (u32)(-6 & 0x7f) << 15 | 30u << 10 | 31u << 5 | 29u);            /* stp x29, x30, [sp, #-48]! */
    a_stp(27, 28, 31, 16); a_stp(25, 26, 31, 32);
    a_mov(28, 0); a_mov(27, 2); a_mov(26, 3); a_br(1);
    jepi = jcur;
    a_ldp(25, 26, 31, 32); a_ldp(27, 28, 31, 16);
    E(0xA8C00000u | 6u << 15 | 30u << 10 | 31u << 5 | 29u);                          /* ldp x29, x30, [sp], #48 */
    E(0xD65F03C0u);                                               /* ret */
    jcur = jbase + JTRAMP;
    j_sync_icache(jbase, jcur);
    g_jstats = getenv("PEMU_JIT_STATS") != NULL;
    return 0;
}
#endif /* HAVE_JIT */

static void run(CPU*c) {
    if (sigsetjmp(g_jb, 1)) {
        int s = g_fault; sigset_t ss; sigemptyset(&ss); sigaddset(&ss, s); sigprocmask(SIG_UNBLOCK, &ss, NULL);
#ifdef HAVE_JIT
        c->patch = 0;
#endif
        deliver_sig(c, s);
    }
    for (;;) {
#ifdef HAVE_JIT
        if (g_jit_on) { if (pend) { c->patch = 0; check_pending(c); } jit_dispatch(c); continue; }
#endif
        if (pend) check_pending(c);
        step(c);
    }
}

/* ------------------------------------------------------------ ELF loading */
typedef struct { u64 entry, phdr, phnum, base; char interp[4096]; int has_interp; } ElfInfo;

static int load_elf(const char*path, ElfInfo*ei) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) { fprintf(stderr, "pemu: %s: %s\n", path, strerror(errno)); return -1; }
    Elf64_Ehdr eh;
    if (pread(fd, &eh, sizeof eh, 0) != sizeof eh || memcmp(eh.e_ident, "\177ELF", 4) || eh.e_ident[EI_CLASS] != ELFCLASS64 || eh.e_machine != EM_X86_64) {
        fprintf(stderr, "pemu: %s: not an x86_64 ELF executable\n", path); close(fd); return -1;
    }
    Elf64_Phdr*ph = malloc((size_t) eh.e_phnum * sizeof *ph);
    if (pread(fd, ph, (size_t) eh.e_phnum * sizeof *ph, (off_t) eh.e_phoff) != (ssize_t)(eh.e_phnum * sizeof *ph)) { close(fd); return -1; }
    u64 lo = ~0ULL, hi = 0;
    for (int k = 0; k < eh.e_phnum; k++) if (ph[k].p_type == PT_LOAD) {
        if (ph[k].p_vaddr < lo) lo = ph[k].p_vaddr;
        if (ph[k].p_vaddr + ph[k].p_memsz > hi) hi = ph[k].p_vaddr + ph[k].p_memsz;
    }
    lo &= ~4095ULL; hi = (hi + 4095) & ~4095ULL;
    u64 base = 0;
    if (eh.e_type == ET_DYN) {
        void*mm = mmap(NULL, hi - lo, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (mm == MAP_FAILED) { perror("pemu: mmap"); return -1; }
        base = (u64) mm - lo;
    }
    u64 phdr_addr = 0;
    for (int k = 0; k < eh.e_phnum; k++) {
        Elf64_Phdr*p = &ph[k];
        if (p->p_type == PT_INTERP && pread(fd, ei->interp, p->p_filesz < 4095 ? p->p_filesz : 4095, (off_t) p->p_offset) > 0) ei->has_interp = 1;
        if (p->p_type == PT_PHDR) phdr_addr = base + p->p_vaddr;
        if (p->p_type != PT_LOAD) continue;
        u64 s = (base + p->p_vaddr) & ~4095ULL, e = (base + p->p_vaddr + p->p_memsz + 4095) & ~4095ULL;
        void*mm = mmap((void*) s, e - s, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (mm == MAP_FAILED) { perror("pemu: mmap segment"); return -1; }
        if (p->p_filesz && pread(fd, (void*)(base + p->p_vaddr), p->p_filesz, (off_t) p->p_offset) != (ssize_t) p->p_filesz) { fprintf(stderr, "pemu: short read\n"); return -1; }
        int prot = ((p->p_flags & PF_R) ? PROT_READ : 0) | ((p->p_flags & PF_W) ? PROT_WRITE : 0) | ((p->p_flags & PF_X) ? PROT_EXEC : 0);
#ifdef HAVE_JIT
        if ((prot & PROT_WRITE) && (prot & PROT_EXEC)) jit_note_rwx(s, e - s);       /* never translated: x86 code may modify itself */
#endif
        mprotect((void*) s, e - s, prot | PROT_READ);
        if (!phdr_addr && p->p_offset <= eh.e_phoff && eh.e_phoff < p->p_offset + p->p_filesz) phdr_addr = base + p->p_vaddr + (eh.e_phoff - p->p_offset);
    }
    ei->entry = base + eh.e_entry; ei->phdr = phdr_addr; ei->phnum = eh.e_phnum; ei->base = base;
    free(ph); close(fd);
    return 0;
}
static u64 push_str(u64*sp, const char*s) { size_t n = strlen(s) + 1; *sp -= n; memcpy((void*)*sp, s, n); return *sp; }

int main(int argc, char**argv) {
    const char*argv0 = NULL; int a = 1, nojit = 0;
#ifdef HAVE_JIT
    { const char*ev = getenv("PEMU_JIT"); if (ev && !strcmp(ev, "0")) nojit = 1; }
#else
    (void) nojit;
#endif
    for (; a < argc && argv[a][0] == '-' && argv[a][1]; a++) {
        const char*o = argv[a];
        if (!strcmp(o, "--")) { a++; break; }
        if (!strcmp(o, "-0") && a + 1 < argc) argv0 = argv[++a];
        else if ((!strcmp(o, "-L") || !strcmp(o, "-cpu") || !strcmp(o, "-s") || !strcmp(o, "-g") || !strcmp(o, "-R")) && a + 1 < argc) a++;
        else if (!strcmp(o, "-E") && a + 1 < argc) { char*v = strdup(argv[++a]); char*eq = strchr(v, '='); if (eq) { *eq = 0; setenv(v, eq + 1, 1); } }
        else if (!strcmp(o, "-U") && a + 1 < argc) unsetenv(argv[++a]);
        else if (!strcmp(o, "-strace")) g_trace = 1;
        else if (!strcmp(o, "-nojit")) nojit = 1;
        else if (!strcmp(o, "-jit")) nojit = 0;
        else if (!strcmp(o, "--version") || !strcmp(o, "-version")) { puts("pemu 0.2 (x86_64 user-mode emulator with AArch64 JIT, qemu-user alternative for prx)"); return 0; }
        else { fprintf(stderr, "pemu: unknown option %s\n", o); return 2; }
    }
    if (a >= argc) { fprintf(stderr, "usage: pemu [-0 argv0] [-strace] [-nojit] program [args...]\n"); return 2; }
    if (getenv("QEMU_STRACE")) g_trace = 1;
    ssize_t sl = readlink("/proc/self/exe", g_self, sizeof g_self - 1);
    if (sl > 0) g_self[sl] = 0; else strcpy(g_self, argv[0]);
    const char*prog = argv[a];
    char*sete = getenv("QEMU_SET_ENV"); if (sete) { sete = strdup(sete); for (char*t = strtok(sete, ","); t; t = strtok(NULL, ",")) { char*eq = strchr(t, '='); if (eq) { *eq = 0; setenv(t, eq + 1, 1); } } }
    char*unse = getenv("QEMU_UNSET_ENV"); if (unse) { unse = strdup(unse); for (char*t = strtok(unse, ","); t; t = strtok(NULL, ",")) unsetenv(t); }
    ElfInfo prg = {0}, itp = {0};
    if (load_elf(prog, &prg) < 0) return 127;
    u64 entry = prg.entry;
    if (prg.has_interp && load_elf(prg.interp, &itp) < 0) return 127;
    if (prg.has_interp) entry = itp.entry;
    brk_end = (u64) mmap(NULL, 1ULL << 30, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    brk_base = brk_cur = brk_end; brk_end += 1ULL << 30;
    g_tramp = (u64) mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memcpy((void*) g_tramp, "\xb8\x0f\x00\x00\x00\x0f\x05", 7);
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = hsegv; sa.sa_flags = SA_SIGINFO | SA_NODEFER; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
    { struct sigaction ss; memset(&ss, 0, sizeof ss); ss.sa_sigaction = hsys; ss.sa_flags = SA_SIGINFO; sigemptyset(&ss.sa_mask); sigaction(SIGSYS, &ss, NULL); }
    size_t ssz = 8UL << 20;
    u64 top = (u64) mmap(NULL, ssz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_STACK, -1, 0) + ssz;
    u64 sp = top;
    u64 execfn = push_str(&sp, prog);
    int ac = argc - a, ec = 0; while (environ[ec]) ec++;
    u64*as = calloc((size_t) ac + 1, 8), *es = calloc((size_t) ec + 1, 8);
    for (int k = ec - 1; k >= 0; k--) es[k] = push_str(&sp, environ[k]);
    for (int k = ac - 1; k >= 0; k--) as[k] = push_str(&sp, k == 0 && argv0 ? argv0 : argv[a + k]);
    u64 plat = push_str(&sp, "x86_64");
    sp &= ~15ULL; sp -= 16; u64 rnd = sp; if (syscall(SYS_getrandom, (void*) rnd, 16, 0) < 0) memset((void*) rnd, 0x5a, 16);
    u64 aux[][2] = {
        {AT_PHDR, prg.phdr}, {AT_PHENT, sizeof(Elf64_Phdr)}, {AT_PHNUM, prg.phnum}, {AT_PAGESZ, 4096},
        {AT_BASE, itp.base}, {AT_FLAGS, 0}, {AT_ENTRY, prg.entry}, {AT_UID, getuid()}, {AT_EUID, geteuid()},
        {AT_GID, getgid()}, {AT_EGID, getegid()}, {AT_HWCAP, 0x078bfbff}, {AT_CLKTCK, 100}, {AT_SECURE, 0},
        {AT_RANDOM, rnd}, {AT_PLATFORM, plat}, {AT_EXECFN, execfn}, {AT_HWCAP2, 0}, {AT_NULL, 0}};
    int na = (int)(sizeof aux / sizeof aux[0]);
    u64 words = 1 + (u64) ac + 1 + (u64) ec + 1 + 2 * (u64) na;
    sp -= words * 8; sp &= ~15ULL;
    u64*w = (u64*) sp; int k = 0;
    w[k++] = (u64) ac;
    for (int j = 0; j < ac; j++) w[k++] = as[j];
    w[k++] = 0;
    for (int j = 0; j < ec; j++) w[k++] = es[j];
    w[k++] = 0;
    for (int j = 0; j < na; j++) { w[k++] = aux[j][0]; w[k++] = aux[j][1]; }
    cpu.r[RSP] = sp; cpu.rip = entry; cpu.rf = 0x202; cpu.mxcsr = 0x1f80; cpu.fcw = 0x37f; cpu.fsw = 0; cpu.top = 0;
#ifdef HAVE_JIT
    if (!nojit && jit_init() == 0) g_jit_on = 1;
#endif
    run(&cpu);
    return 0;
}
