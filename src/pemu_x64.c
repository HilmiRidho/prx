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
 * CPU model: x86-64-v2 (SSE2 to SSE4.2, CMPXCHG16B, POPCNT) with an x87 subset. cpuid reports only
 * what is implemented: every instruction of an advertised feature must exist, because glibc picks its code paths from it.
 * Threads (CLONE_THREAD) are supported: each guest thread runs on its own host pthread with its own CPU state.
 * NOT supported: AVX/AVX2/FMA/BMI/AES/XSAVE, parts of x87, 32-bit code.
 *
 * JIT (aarch64 hosts): guest basic blocks are translated to AArch64 code (see the "JIT" section below). Anything the
 * translator does not know runs through the interpreter from inside the translated block, so behaviour is identical.
 * Disable it with PEMU_JIT=0 or -nojit. The JIT works with guest threads (one shared cache, see JT).
 * PEMU_JIT_OPT=<mask> (default 127) toggles optional translations: 1 = lazy flags for shift-by-immediate, 2 = scalar SSE float,
 * 4 = guest rax..rdi pinned to host registers, 8 = superblocks, 16 = return stack buffer, 32 = comiss/comisd/ucomiss/ucomisd with the host
 * flags of fcmp, 64 = div/idiv through udiv/sdiv (only when the dividend fits and the divisor is not 0 or -1; the rest is interpreted).
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

#define RSB_N 64
typedef struct {
    u64 r[16];
    u64 rip, rf, fs, gs;
    u64 la, lb, lr;                                   /* lazy arithmetic flags: operands/result of the last add/sub/logic op */
    union { struct { u8 lop, lsz, lcin; }; u64 lmeta; };   /* lmeta = lop | lsz << 8 | lcin << 16 (the JIT stores it in one go) */
    u64 patch;                                        /* JIT: chain patch site left by an exit stub (bit 0: it is a bl); 1 = interpret one instruction */
    u64 rsbi; u64 rsb[RSB_N][2];                      /* JIT return stack buffer: ring of (guest return address, host continuation) pairs, filled by call, checked by ret */
    u64 mm[8];                                        /* MMX registers (kept apart from the x87 stack they alias on real hardware) */
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
static volatile int g_jit_on;                 /* JIT active */
static void jit_fault(void*uctx, CPU*c);
static void jit_post_syscall(CPU*c, u64 n, long r);
static void jit_note_rwx(u64 start, u64 len);
static void jit_report(void);
static void jit_thread_init(void);
static void jit_thread_exit(void);
static void jit_region_abort(void);
static void jit_rsb_clear(CPU *c);
static void jit_mt_enable(void);
static void jit_atfork(int phase);
#endif

/* ------------------------------------------------------------ helpers */
static long hs(long nr, long a, long b, long c_, long d, long e, long f) {
    long r = syscall(nr, a, b, c_, d, e, f);
    return r == -1 ? -errno : r;
}
static inline u64 mskz(int sz) { return sz == 8 ? ~0ULL : (1ULL << (sz * 8)) - 1; }
static inline int sgn(u64 v, int sz) { return (int)((v >> (sz * 8 - 1)) & 1); }
static inline s64 sx(u64 v, int sz) { return sz == 8 ? (s64) v : (s64)(v << (64 - sz * 8)) >> (64 - sz * 8); }
/* Set once the guest has created a second thread. x86 is TSO, aarch64 is weakly ordered: from then on every interpreted
 * load is followed by an acquire barrier and every store is preceded by a release barrier, which gives the x86 ordering
 * (only store->load may be reordered). Free on x86 hosts (compiler barrier only). SSE/x87 accesses do not go through
 * mrd/mwr and are not covered. */
static int g_mt;
static u64 mrd(u64 a, int n) {
    u64 v;
    switch (n) { case 1: v = *(u8*) a; break; case 2: v = *(u16u*) a; break; case 4: v = *(u32u*) a; break; default: v = *(u64u*) a; }
    if (__builtin_expect(__atomic_load_n(&g_mt, __ATOMIC_RELAXED), 0)) __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return v;
}
static void mwr(u64 a, u64 v, int n) {
    if (__builtin_expect(__atomic_load_n(&g_mt, __ATOMIC_RELAXED), 0)) __atomic_thread_fence(__ATOMIC_RELEASE);
    switch (n) { case 1: *(u8*) a = (u8) v; break; case 2: *(u16u*) a = (u16) v; break; case 4: *(u32u*) a = (u32) v; break; default: *(u64u*) a = v; }
}

/* ---- lazy flags: add/sub/logic only record their operands; CF/OF/AF/ZF/SF/PF are computed when somebody reads them.
 * RF(c) is an lvalue that first materialises any pending flags, so every other use of the flags stays unchanged. */
enum { LZ_NONE, LZ_ADD, LZ_SUB, LZ_LOGIC, LZ_SHL, LZ_SHR, LZ_SAR, LZ_FCMP };   /* LZ_SH*: la = value, lb = count (1..bits-1), lr = result (only recorded by the JIT); LZ_FCMP: la, lb = the two operands of comiss/comisd (bit patterns, lsz 4 or 8), only recorded by the JIT */
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
    if (c->lop == LZ_FCMP) {                                    /* same flags as the interpreter's comisd: unordered 111, below CF, equal ZF */
        double x, y;
        if (sz == 8) { memcpy(&x, &a, 8); memcpy(&y, &b, 8); } else { float fx, fy; u32 ua = (u32) a, ub = (u32) b; memcpy(&fx, &ua, 4); memcpy(&fy, &ub, 4); x = fx; y = fy; }
        if (isnan(x) || isnan(y)) f |= ZF_ | PF_ | CF_; else if (x < y) f |= CF_; else if (x == y) f |= ZF_;
        c->rf = f; c->lop = LZ_NONE; return;
    }
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
    case LZ_SHL:
        if ((a >> (sb + 1 - (int) b)) & 1) f |= CF_;
        if ((int)(r >> sb & 1) ^ (int)((a >> (sb + 1 - (int) b)) & 1)) f |= OF_;
        break;
    case LZ_SHR:
        if ((a >> (b - 1)) & 1) f |= CF_;
        if (a >> sb & 1) f |= OF_;
        break;
    case LZ_SAR:
        if ((u64)(sx(a, sz) >> (b - 1)) & 1) f |= CF_;
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
static volatile u64 pend;                          /* signals received by the main thread (the JIT polls this word) */
static __thread volatile u64 tpend;                /* signals received by a CLONE_THREAD thread: delivered to that same thread, like the kernel does */
static __thread siginfo_t pinfo[65];
#define PENDP() (g_is_thread ? &tpend : &pend)
/* sigaltstack state of the calling guest thread (a new thread starts without one) */
static __thread u64 g_ss_sp, g_ss_size;
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
static void hsig(int s, siginfo_t*si, void*u) { (void) u; if (si) pinfo[s] = *si; __atomic_or_fetch(PENDP(), 1ULL << s, __ATOMIC_SEQ_CST); }
static void hsegv(int s, siginfo_t*si, void*u) {
#ifdef HAVE_JIT
    if (g_jit_on) jit_fault(u, g_cur);                                     /* fault inside translated code: recover the guest rip */
#else
    (void) u;
#endif
    if (gsa[s].handler > 1) { g_fault = s; g_faddr = (u64) si->si_addr; pinfo[s] = *si; siglongjmp(g_jb, 1); }
    fprintf(stderr, "pemu: guest fault (signal %d) at addr=%p rip=%#llx\n", s, si->si_addr, (unsigned long long) g_cur->rip);
    signal(s, SIG_DFL); raise(s);
    _exit(128 + s);
}
static void fx_save(CPU*c, u8*p);                      /* FXSAVE image: fxsave, signal frame (defined with the x87 code) */
static void fx_restore(CPU*c, const u8*p);
static const int gidx[16] = {8, 9, 10, 11, 12, 13, 14, 15, 7, 6, 5, 3, 2, 0, 1, 4};
#define SFRAME 1088
static void deliver_sig(CPU*c, int s) {
    struct gsa*h = &gsa[s];
    int on_alt = g_ss_size && c->r[RSP] >= g_ss_sp && c->r[RSP] - g_ss_sp < g_ss_size;     /* interrupted code already runs on the alternate stack */
    u64 top = ((h->flags & 0x08000000u /*SA_ONSTACK*/) && g_ss_size && !on_alt) ? g_ss_sp + g_ss_size : c->r[RSP] - 128;
    u64 sp = ((top - SFRAME) & ~15ULL) - 8;
    memset((void*) sp, 0, SFRAME);
    u64 uc = sp + 8, info = sp + 8 + 424, fp = sp + 568;
    memcpy((void*) info, &pinfo[s], 128);
    u64 old = 0;
    syscall(SYS_rt_sigprocmask, SIG_BLOCK, NULL, &old, 8);
    *(u64*)(uc + 296) = old;
    *(u64*)(uc + 16) = g_ss_sp; *(u32*)(uc + 24) = !g_ss_size ? 2 /*SS_DISABLE*/ : on_alt ? 1 /*SS_ONSTACK*/ : 0; *(u64*)(uc + 32) = g_ss_size;   /* uc_stack */
    u64 mc = uc + 40;
    for (int i = 0; i < 16; i++) *(u64*)(mc + 8 * (u64) i) = c->r[gidx[i]];
    *(u64*)(mc + 8 * 16) = c->rip; *(u64*)(mc + 8 * 17) = RF(c);
    *(u64*)(mc + 8 * 22) = g_fault == s ? g_faddr : 0;
    *(u64*)(mc + 8 * 23) = fp;
    fx_save(c, (u8*) fp);
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
    if (fp) fx_restore(c, (const u8*) fp);
    u64 m = *(u64*)(uc + 296);
    syscall(SYS_rt_sigprocmask, SIG_SETMASK, &m, NULL, 8);
}
static void check_pending(CPU*c) {
    volatile u64 *pp = PENDP();
    while (*pp) {
        u64 p = *pp; int s = __builtin_ctzll(p);
        __atomic_and_fetch(pp, ~(1ULL << s), __ATOMIC_SEQ_CST);
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
/* more pass-through syscalls (structures have the same layout on x86_64 and the generic aarch64 ABI) */
#ifdef SYS_shmget
    P2(29, SYS_shmget)
#endif
#ifdef SYS_shmat
    P2(30, SYS_shmat)
#endif
#ifdef SYS_shmdt
    P2(67, SYS_shmdt)
#endif
#ifdef SYS_semget
    P2(64, SYS_semget)
#endif
#ifdef SYS_semop
    P2(65, SYS_semop)
#endif
#ifdef SYS_semtimedop
    P2(220, SYS_semtimedop)
#endif
#ifdef SYS_msgget
    P2(68, SYS_msgget)
#endif
#ifdef SYS_msgsnd
    P2(69, SYS_msgsnd)
#endif
#ifdef SYS_msgrcv
    P2(70, SYS_msgrcv)
#endif
#ifdef SYS_rt_sigqueueinfo
    P2(129, SYS_rt_sigqueueinfo)
#endif
#ifdef SYS_rt_tgsigqueueinfo
    P2(297, SYS_rt_tgsigqueueinfo)
#endif
#ifdef SYS_settimeofday
    P2(164, SYS_settimeofday)
#endif
#ifdef SYS_adjtimex
    P2(159, SYS_adjtimex)
#endif
#ifdef SYS_syslog
    P2(103, SYS_syslog)
#endif
#ifdef SYS_setdomainname
    P2(171, SYS_setdomainname)
#endif
#ifdef SYS_vhangup
    P2(153, SYS_vhangup)
#endif
#ifdef SYS_readahead
    P2(187, SYS_readahead)
#endif
#ifdef SYS_ioprio_set
    P2(251, SYS_ioprio_set)
#endif
#ifdef SYS_ioprio_get
    P2(252, SYS_ioprio_get)
#endif
#ifdef SYS_unshare
    P2(272, SYS_unshare)
#endif
#ifdef SYS_setns
    P2(308, SYS_setns)
#endif
#ifdef SYS_process_vm_readv
    P2(310, SYS_process_vm_readv)
#endif
#ifdef SYS_process_vm_writev
    P2(311, SYS_process_vm_writev)
#endif
#ifdef SYS_sched_setattr
    P2(314, SYS_sched_setattr)
#endif
#ifdef SYS_sched_getattr
    P2(315, SYS_sched_getattr)
#endif
#ifdef SYS_fanotify_init
    P2(300, SYS_fanotify_init)
#endif
#ifdef SYS_fanotify_mark
    P2(301, SYS_fanotify_mark)
#endif
#ifdef SYS_name_to_handle_at
    P2(303, SYS_name_to_handle_at)
#endif
#ifdef SYS_open_by_handle_at
    P2(304, SYS_open_by_handle_at)
#endif
#ifdef SYS_clock_adjtime
    P2(305, SYS_clock_adjtime)
#endif
#ifdef SYS_membarrier
    P2(324, SYS_membarrier)
#endif
#ifdef SYS_mlock2
    P2(325, SYS_mlock2)
#endif
#ifdef SYS_preadv2
    P2(327, SYS_preadv2)
#endif
#ifdef SYS_pwritev2
    P2(328, SYS_pwritev2)
#endif
#ifdef SYS_userfaultfd
    P2(323, SYS_userfaultfd)
#endif
#ifdef SYS_io_setup
    P2(206, SYS_io_setup)
#endif
#ifdef SYS_io_destroy
    P2(207, SYS_io_destroy)
#endif
#ifdef SYS_io_getevents
    P2(208, SYS_io_getevents)
#endif
#ifdef SYS_io_submit
    P2(209, SYS_io_submit)
#endif
#ifdef SYS_io_cancel
    P2(210, SYS_io_cancel)
#endif
#ifdef SYS_mq_open
    P2(240, SYS_mq_open)
#endif
#ifdef SYS_mq_unlink
    P2(241, SYS_mq_unlink)
#endif
#ifdef SYS_mq_timedsend
    P2(242, SYS_mq_timedsend)
#endif
#ifdef SYS_mq_timedreceive
    P2(243, SYS_mq_timedreceive)
#endif
#ifdef SYS_mq_notify
    P2(244, SYS_mq_notify)
#endif
#ifdef SYS_mq_getsetattr
    P2(245, SYS_mq_getsetattr)
#endif
#ifdef SYS_add_key
    P2(248, SYS_add_key)
#endif
#ifdef SYS_request_key
    P2(249, SYS_request_key)
#endif
#ifdef SYS_keyctl
    P2(250, SYS_keyctl)
#endif
#ifdef SYS_mbind
    P2(237, SYS_mbind)
#endif
#ifdef SYS_set_mempolicy
    P2(238, SYS_set_mempolicy)
#endif
#ifdef SYS_get_mempolicy
    P2(239, SYS_get_mempolicy)
#endif
#ifdef SYS_migrate_pages
    P2(256, SYS_migrate_pages)
#endif
#ifdef SYS_move_pages
    P2(279, SYS_move_pages)
#endif
#ifdef SYS_faccessat2
    P2(439, SYS_faccessat2)
#endif
#ifdef SYS_epoll_pwait2
    P2(441, SYS_epoll_pwait2)
#endif
#ifdef SYS_pidfd_getfd
    P2(438, SYS_pidfd_getfd)
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
#ifdef HAVE_JIT
    if (g_jit_on) jit_thread_exit();
#endif
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
/* From now on emulate x86 memory ordering: another agent (a second guest thread, or the kernel writing an io_uring ring) is
 * about to touch guest memory concurrently, and an aarch64 host would otherwise reorder the interpreted and translated accesses. */
static void mt_barriers(void) {
#ifdef HAVE_JIT
    if (g_jit_on) jit_mt_enable();                        /* translated code from now on carries the memory barriers of x86 ordering */
#endif
    __atomic_store_n(&g_mt, 1, __ATOMIC_SEQ_CST);        /* see mrd/mwr */
}
static long clone_thread(CPU*c, u64 flags, u64 stack, u64 ptid, u64 ctid, u64 tls) {
    if ((flags & 0x100) == 0 || (flags & 0x800) == 0) return -EINVAL;   /* threads need CLONE_VM | CLONE_SIGHAND */
    ThArg*t = malloc(sizeof *t);
    if (!t) return -ENOMEM;
    mt_barriers();
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

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
/* MAP_32BIT: x86-64 guests ask for an address in the first 2 GiB (JIT code, some allocators). The aarch64 host has no such
 * flag, so look for a free range there ourselves; the guest is mapped 1:1, so any address that mmap accepts is valid. */
static long mmap32(u64 len, u64 prot, u64 flags, u64 fd, u64 off) {
    static u64 next = 0x10000000ULL;
    const u64 lo = 0x10000000ULL, hi = 0x80000000ULL, pg = (u64) sysconf(_SC_PAGESIZE);
    u64 n = (len + pg - 1) & ~(pg - 1), a = __atomic_load_n(&next, __ATOMIC_RELAXED);
    if (!n) return -EINVAL;
    for (int k = 0; k < 8192; k++) {
        if (a < lo || a + n > hi) a = lo;
        long r = hs(SYS_mmap, (long) a, (long) len, (long) prot, (long) (flags | MAP_FIXED_NOREPLACE), (long) fd, (long) off);
        if (r == (long) a) { __atomic_store_n(&next, a + n, __ATOMIC_RELAXED); return r; }
        if (r >= 0) munmap((void*) r, n);                 /* a kernel without MAP_FIXED_NOREPLACE took it as a hint */
        else if (r != -EEXIST) return r;
        a += n < 0x10000 ? 0x10000 : n;
    }
    return -ENOMEM;
}

/* The emulated CPU is an Intel Core i7-920 (Bloomfield, Nehalem, D0 stepping): family 6 model 0x1a stepping 5, 2.66 GHz, 4 cores,
 * 32K+32K L1, 256K L2, 8M L3. It is the real CPU whose feature set matches what pemu implements (x86-64-v2: SSE4.2, POPCNT,
 * CMPXCHG16B, no AES/PCLMUL/AVX). cpuid() and /proc/cpuinfo both take their identity from here, so they cannot disagree. */
#define PEMU_BRAND     "Intel(R) Core(TM) i7 CPU         920  @ 2.67GHz"     /* 47 characters: leaves 0x80000002-4 hold 48 bytes */
#define PEMU_MHZ_STR   "2666.667"
#define PEMU_BOGO_STR  "5333.33"                                              /* Linux prints about 2 x MHz */

/* /proc/cpuinfo as an x86 CPU. The host's file lists ARM flags (asimd, fp, ...), so a guest that decides what the CPU can do by
 * parsing it finds no sse3/pni and refuses to run. The text is consistent with cpuid (see cpuid()). */
static const char cpuinfo_flags[] = "fpu tsc cx8 cmov mmx fxsr sse sse2 syscall nx lm constant_tsc nopl pni ssse3 cx16 sse4_1 sse4_2 popcnt lahf_lm";
static long cpuinfo_fd(long hostflags) {
    long n = sysconf(_SC_NPROCESSORS_ONLN); if (n < 1) n = 1; if (n > 256) n = 256;
    size_t cap = (size_t) n * 1100 + 64, len = 0; char*b = malloc(cap);
    if (!b) return -ENOMEM;
    for (int i = 0; i < n; i++)
        len += (size_t) snprintf(b + len, cap - len,
            "processor\t: %d\nvendor_id\t: GenuineIntel\ncpu family\t: 6\nmodel\t\t: 26\nmodel name\t: " PEMU_BRAND "\n"
            "stepping\t: 5\ncpu MHz\t\t: " PEMU_MHZ_STR "\ncache size\t: 8192 KB\nphysical id\t: 0\nsiblings\t: %ld\ncore id\t\t: %d\ncpu cores\t: %ld\n"
            "apicid\t\t: %d\ninitial apicid\t: %d\nfpu\t\t: yes\nfpu_exception\t: yes\ncpuid level\t: 4\nwp\t\t: yes\nflags\t\t: %s\n"
            "bogomips\t: " PEMU_BOGO_STR "\nclflush size\t: 64\ncache_alignment\t: 64\naddress sizes\t: 40 bits physical, 48 bits virtual\npower management:\n\n",
            i, n, i, n, i, i, cpuinfo_flags);
    int cloexec = (hostflags & O_CLOEXEC) != 0; long fd = -1;
#ifdef SYS_memfd_create
    fd = syscall(SYS_memfd_create, "pemu-cpuinfo", cloexec ? 1 /*MFD_CLOEXEC*/ : 0);
    if (fd >= 0) {
        if (write((int) fd, b, len) != (ssize_t) len || lseek((int) fd, 0, SEEK_SET) < 0) { close((int) fd); fd = -1; }
    }
#endif
    if (fd < 0 && len <= 60000) {                       /* no memfd (blocked, old kernel): a pipe is enough for reading it once */
        int p[2];
        if (pipe2(p, cloexec ? O_CLOEXEC : 0) == 0) {
            if (write(p[1], b, len) == (ssize_t) len) { close(p[1]); fd = p[0]; } else { close(p[0]); close(p[1]); }
        }
    }
    free(b);
    return fd >= 0 ? fd : -EACCES;
}
static int is_cpuinfo(const char*p, long hostflags) { return p && !strcmp(p, "/proc/cpuinfo") && (hostflags & O_ACCMODE) == O_RDONLY; }

static long gsys(CPU*c, int*noset) {
    u64 n = c->r[RAX], a0 = c->r[RDI], a1 = c->r[RSI], a2 = c->r[RDX], a3 = c->r[10], a4 = c->r[8], a5 = c->r[9];
    long r, nr;
    switch (n) {
#ifdef HAVE_JIT
    case 231: jit_report(); return hs(SYS_exit_group, (long) a0, 0, 0, 0, 0, 0);
#else
    case 231: return hs(SYS_exit_group, (long) a0, 0, 0, 0, 0, 0);
#endif
    case 60:
        if (g_is_thread) thread_exit(c);                /* does not return */
#ifdef HAVE_JIT
        jit_report();
#endif
        return hs(SYS_exit_group, (long) a0, 0, 0, 0, 0, 0);
    case 2: { long hf = fl_g2h((long) a1); if (is_cpuinfo((const char*) a0, hf)) return cpuinfo_fd(hf);
              return hs(SYS_openat, AT_FDCWD, (long) a0, hf, (long) a2, 0, 0); }
    case 85: return hs(SYS_openat, AT_FDCWD, (long) a0, O_CREAT | O_WRONLY | O_TRUNC, (long) a1, 0, 0);
    case 257: { long hf = fl_g2h((long) a2); if (is_cpuinfo((const char*) a1, hf)) return cpuinfo_fd(hf);
                return hs(SYS_openat, (long) a0, (long) a1, hf, (long) a3, 0, 0); }
#ifdef SYS_openat2
    case 437: {                                                                                             /* openat2: how->flags holds O_* flags, which differ between x86_64 and arm64 */
        u64 how[8] = {0}; u64 sz = a3;
        if (sz < 24) return -EINVAL;
        if (sz > sizeof how) return -E2BIG;
        memcpy(how, (const void*) a2, (size_t) sz); how[0] = (u64) fl_g2h((long) how[0]);
        if (is_cpuinfo((const char*) a1, (long) how[0])) return cpuinfo_fd((long) how[0]);
        return hs(SYS_openat2, (long) a0, (long) a1, (long) how, (long) sz, 0, 0); }
#endif
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
    case 131: {                                       /* sigaltstack(const stack_t *ss, stack_t *old): sp@0 flags@8 size@16 */
        int on = g_ss_size && c->r[RSP] >= g_ss_sp && c->r[RSP] - g_ss_sp < g_ss_size;
        u64 osp = g_ss_sp, osz = g_ss_size; u32 ofl = !g_ss_size ? 2 : on ? 1 : 0;
        if (a0) {
            u64 nsp = *(u64u*) a0, nsz = *(u64u*)(a0 + 16); u32 nfl = *(u32u*)(a0 + 8);
            if (on) return -EPERM;                                    /* cannot change it while running on it */
            if (nfl & ~(u32)(2 | 0x80000000u)) return -EINVAL;        /* SS_DISABLE, SS_AUTODISARM */
            if (nfl & 2) { g_ss_sp = 0; g_ss_size = 0; }
            else { if (nsz < 2048 /*MINSIGSTKSZ*/) return -ENOMEM; g_ss_sp = nsp; g_ss_size = nsz; }
        }
        if (a1) { *(u64u*) a1 = osp; *(u32u*)(a1 + 8) = ofl; *(u32u*)(a1 + 12) = 0; *(u64u*)(a1 + 16) = osz; }
        return 0; }
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
        if (!ev) return -ENOMEM;
        r = hs(SYS_epoll_pwait, (long) a0, (long) ev, max, (long) a3, n == 281 ? (long) a4 : 0, 8);
        for (long i = 0; i < r; i++) { u8*g = (u8*) a1 + 12 * i; *(u32*) g = ev[i].events; *(u64u*)(g + 4) = ev[i].data.u64; }
        free(ev); return r;
    }
    case 213: return hs(SYS_epoll_create1, 0, 0, 0, 0, 0, 0);
    case 284: return hs(SYS_eventfd2, (long) a0, 0, 0, 0, 0, 0);
    case 56: case 57: case 58: {                      /* clone / fork / vfork */
        u64 flags = n == 56 ? a0 : SIGCHLD;
        if (flags & 0x10000 /*CLONE_THREAD*/) return clone_thread(c, flags, a1, a2, a3, a4);
#ifdef HAVE_JIT
        if (g_jit_on) jit_atfork(0);                  /* no other thread may hold the JIT lock across the fork */
#endif
        long pid = fork(); int fe = errno;
#ifdef HAVE_JIT
        if (g_jit_on) jit_atfork(pid == 0 ? 2 : 1);
#endif
        if (pid < 0) return -fe;
        if (pid == 0) {
            if (n == 56 && a1) c->r[RSP] = a1;
            if (n == 56 && (flags & 0x01000000)) *(u32*) a3 = (u32) getpid();     /* CHILD_SETTID: ctid */
            if (n == 56 && (flags & 0x00080000)) c->fs = a4;                        /* SETTLS */
            c->r[RAX] = 0; *noset = 1;
        } else if (n == 56 && (flags & 0x00100000)) *(u32*) a2 = (u32) pid;      /* PARENT_SETTID */
        return pid;
    }
    case 59: return do_execve((const char*) a0, (char**) a1, (char**) a2);
    case 9:
        if (a3 & 0x40 /*MAP_32BIT*/ && !a0) return mmap32(a1, a2, a3 & ~0x40UL & ~(u64) MAP_FIXED, a4, a5);
        return hs(SYS_mmap, (long) a0, (long) a1, (long) a2, (long)(a3 & ~0x40UL), (long) a4, (long) a5);
#ifdef SYS_shmctl
    case 31: return hs(SYS_shmctl, (long) a0, (long)(a1 & ~0x100UL), (long) a2, 0, 0, 0);                   /* IPC_64 is implied on 64-bit; the generic ABI rejects the flag */
#endif
#ifdef SYS_semctl
    case 66: return hs(SYS_semctl, (long) a0, (long) a1, (long)(a2 & ~0x100UL), (long) a3, 0, 0);
#endif
#ifdef SYS_msgctl
    case 71: return hs(SYS_msgctl, (long) a0, (long)(a1 & ~0x100UL), (long) a2, 0, 0, 0);
#endif
    case 132: { struct timespec ts[2], *tp = NULL;                                                          /* utime(path, struct utimbuf*) */
                if (a1) { const s64*u = (const s64*) a1; ts[0].tv_sec = (time_t) u[0]; ts[0].tv_nsec = 0; ts[1].tv_sec = (time_t) u[1]; ts[1].tv_nsec = 0; tp = ts; }
                return hs(SYS_utimensat, AT_FDCWD, (long) a0, (long) tp, 0, 0, 0); }
    case 133: return hs(SYS_mknodat, AT_FDCWD, (long) a0, (long) a1, (long) a2, 0, 0);
    case 261: { struct timespec ts[2], *tp = NULL;                                                          /* futimesat(dirfd, path, struct timeval[2]) */
                if (a2) { struct timeval*tv = (struct timeval*) a2; for (int i = 0; i < 2; i++) { ts[i].tv_sec = tv[i].tv_sec; ts[i].tv_nsec = tv[i].tv_usec * 1000L; } tp = ts; }
                return hs(SYS_utimensat, (long) a0, (long) a1, (long) tp, 0, 0, 0); }
    case 253: return hs(SYS_inotify_init1, 0, 0, 0, 0, 0, 0);
    case 282: return hs(SYS_signalfd4, (long) a0, (long) a1, (long) a2, 0, 0, 0);
    case 322: case 435: case 334: case 317: return -ENOSYS;
    case 425: { long r = hs(425 /*io_uring_setup; same number on x86_64 and aarch64*/, (long) a0, (long) a1, 0, 0, 0, 0);
                if (r >= 0) mt_barriers();                                                                  /* the kernel now writes the rings behind our back */
                return r; }
    case 426: return hs(426, (long) a0, (long) a1, (long) a2, (long) a3, (long) a4, (long) a5);         /* io_uring_enter */
    case 427: return hs(427, (long) a0, (long) a1, (long) a2, (long) a3, 0, 0);                         /* io_uring_register */
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
typedef struct { u8*ip; int rex, w, p66, rep, repne, addr32, lock; u64 seg; } I;
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
/* ---- atomic read-modify-write on guest memory, for lock-prefixed instructions and xchg with a memory operand.
 * ARMW re-evaluates EXPR (which may set flags in c and may read `oldv`) until the compare-and-swap succeeds, so the
 * flags always come from the successful iteration. Misaligned operands (split locks) cannot be atomic on aarch64:
 * ATOMIC_OK is false for them and the caller falls back to the plain read-modify-write. */
#define ARMW_T(T, ea_, oldv, EXPR) do { T *armw_p = (T*)(ea_); T armw_cur = __atomic_load_n(armw_p, __ATOMIC_RELAXED); \
    for (;;) { u64 oldv = armw_cur; u64 armw_new = (EXPR); \
        if (__atomic_compare_exchange_n(armw_p, &armw_cur, (T) armw_new, 1, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED)) break; } } while (0)
#define ARMW(ea_, sz_, oldv, EXPR) do { switch (sz_) { \
    case 1: ARMW_T(u8, ea_, oldv, EXPR); break; case 2: ARMW_T(u16, ea_, oldv, EXPR); break; \
    case 4: ARMW_T(u32, ea_, oldv, EXPR); break; default: ARMW_T(u64, ea_, oldv, EXPR); } } while (0)
#define ATOMIC_OK(in_, m_, sz_) ((in_)->lock && (m_)->ismem && !((m_)->ea & ((u64)(sz_) - 1)))
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

/* ------------------------------------------------------------ MMX (no prefix 0F xx, and the SSSE3 forms of 0F 38 / 0F 3A)
 * mm0-mm7 live in c->mm. On real hardware they alias the x87 registers; here they are separate, which is only visible to code
 * that mixes MMX and x87 without emms (and emms is then a no-op). The lane operations of the SSE2 integer set (pint) work on
 * the low 64 bits unchanged; pack, punpckh*, pshufw, pextrw/pinsrw, pmovmskb and the shifts by immediate are done here. */
static int mmx_op(int op) {
    return (op >= 0x60 && op <= 0x6B) || op == 0x6E || op == 0x6F || (op >= 0x70 && op <= 0x77) || op == 0x7E || op == 0x7F ||
           op == 0xC4 || op == 0xC5 || (op >= 0xD1 && op <= 0xD5) || (op >= 0xD7 && op <= 0xDF) || (op >= 0xE0 && op <= 0xE5) ||
           (op >= 0xE7 && op <= 0xEF) || (op >= 0xF1 && op <= 0xFE);
}
static int mmx(CPU*c, I*in, int op) {
    if (op == 0x77) return 0;                                   /* emms */
    M m; int imm8 = 0, ok = 1;
    int immb = op == 0x70 || op == 0xC4 || op == 0xC5 || (op >= 0x71 && op <= 0x73);
    modrm(c, in, &m, immb);
    if (immb) imm8 = *in->ip++;
    u64*d = &c->mm[m.reg & 7], sv = 0;
#define MSRC() do { sv = m.ismem ? *(u64u*) m.ea : c->mm[m.rm & 7]; } while (0)
    switch (op) {
    case 0x6E: *d = rdm(c, in, &m, in->w ? 8 : 4); return 0;                                  /* movd/movq mm, r/m */
    case 0x7E: wrm(c, in, &m, in->w ? 8 : 4, *d); return 0;                                  /* movd/movq r/m, mm */
    case 0x6F: MSRC(); *d = sv; return 0;                                                     /* movq mm, mm/m64 */
    case 0x7F: if (m.ismem) *(u64u*) m.ea = *d; else c->mm[m.rm & 7] = *d; return 0;
    case 0xE7: if (!m.ismem) return -1; *(u64u*) m.ea = *d; return 0;                        /* movntq */
    case 0x70: { MSRC(); u64 r = 0;                                                           /* pshufw */
        for (int i = 0; i < 4; i++) r |= ((sv >> (16 * ((imm8 >> (2 * i)) & 3))) & 0xffff) << (16 * i);
        *d = r; return 0; }
    case 0x71: case 0x72: case 0x73: {                                                        /* shift groups by imm8 */
        if (m.ismem) return -1;
        int ext = m.reg & 7, sop = -1;
        if (op == 0x71) sop = ext == 2 ? 0xD1 : ext == 4 ? 0xE1 : ext == 6 ? 0xF1 : -1;
        else if (op == 0x72) sop = ext == 2 ? 0xD2 : ext == 4 ? 0xE2 : ext == 6 ? 0xF2 : -1;
        else sop = ext == 2 ? 0xD3 : ext == 6 ? 0xF3 : -1;
        if (sop < 0) return -1;
        V a = {{0}}, n = {{0}}; a.q[0] = c->mm[m.rm & 7]; n.q[0] = (u8) imm8;
        pint(sop, &a, &n, &ok); c->mm[m.rm & 7] = a.q[0]; return 0; }
    case 0xC4: { u64 v = m.ismem ? mrd(m.ea, 2) : getr(c, m.rm, 4, 0); int sh = 16 * (imm8 & 3);          /* pinsrw */
        *d = (*d & ~(0xffffULL << sh)) | ((v & 0xffff) << sh); return 0; }
    case 0xC5: if (m.ismem) return -1; setr(c, m.reg, 4, 0, (c->mm[m.rm & 7] >> (16 * (imm8 & 3))) & 0xffff); return 0;       /* pextrw */
    case 0xD7: { if (m.ismem) return -1; u64 r = c->mm[m.rm & 7], v = 0;                                 /* pmovmskb */
        for (int i = 0; i < 8; i++) v |= ((r >> (8 * i + 7)) & 1) << i;
        setr(c, m.reg, 4, 0, v); return 0; }
    case 0xF7: { if (m.ismem) return -1; u64 mk = c->mm[m.rm & 7], dv = *d; u8*dst = (u8*)(c->r[RDI] + in->seg);   /* maskmovq */
        for (int i = 0; i < 8; i++) if ((mk >> (8 * i + 7)) & 1) dst[i] = (u8)(dv >> (8 * i));
        return 0; }
    case 0x63: case 0x67: case 0x6B: case 0x68: case 0x69: case 0x6A: {
        MSRC(); V a = {{0}}, b = {{0}}, r = {{0}}; a.q[0] = *d; b.q[0] = sv;
        switch (op) {
        case 0x63: for (int i = 0; i < 4; i++) { int v = a.sw[i], w = b.sw[i]; r.sb[i] = (s8)(v > 127 ? 127 : v < -128 ? -128 : v); r.sb[4 + i] = (s8)(w > 127 ? 127 : w < -128 ? -128 : w); } break;       /* packsswb */
        case 0x67: for (int i = 0; i < 4; i++) { int v = a.sw[i], w = b.sw[i]; r.b[i] = (u8)(v > 255 ? 255 : v < 0 ? 0 : v); r.b[4 + i] = (u8)(w > 255 ? 255 : w < 0 ? 0 : w); } break;             /* packuswb */
        case 0x6B: for (int i = 0; i < 2; i++) { s64 v = a.sd[i], w = b.sd[i]; r.sw[i] = (s16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); r.sw[2 + i] = (s16)(w > 32767 ? 32767 : w < -32768 ? -32768 : w); } break;   /* packssdw */
        case 0x68: for (int i = 0; i < 4; i++) { r.b[2 * i] = a.b[4 + i]; r.b[2 * i + 1] = b.b[4 + i]; } break;       /* punpckhbw */
        case 0x69: for (int i = 0; i < 2; i++) { r.w[2 * i] = a.w[2 + i]; r.w[2 * i + 1] = b.w[2 + i]; } break;       /* punpckhwd */
        default: r.d[0] = a.d[1]; r.d[1] = b.d[1]; break;                                                            /* punpckhdq */
        }
        *d = r.q[0]; return 0; }
    default: {                                                                                /* the rest is lane-wise: the low half of the SSE2 operation */
        MSRC(); V a = {{0}}, b = {{0}}; a.q[0] = *d; b.q[0] = sv;
        pint(op, &a, &b, &ok);
        if (!ok) return -1;
        *d = a.q[0]; return 0; }
    }
#undef MSRC
}

static int sse(CPU*c, I*in, int op) {
    int pf = in->rep ? 3 : in->repne ? 2 : in->p66 ? 1 : 0, imm8 = 0, ok = 1;
    M m; V sv, *s = NULL;
    if (pf == 0 && mmx_op(op)) return mmx(c, in, op);                  /* no prefix and one of the MMX opcodes: mm registers, not xmm */
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
    case 0x12:
        if (pf == 3) { SRC(16); d->d[0] = d->d[1] = s->d[0]; d->d[2] = d->d[3] = s->d[2]; }       /* movsldup */
        else if (pf == 2) { SRC(8); d->q[0] = d->q[1] = s->q[0]; }                                 /* movddup */
        else if (m.ismem) d->q[0] = *(u64u*) m.ea; else d->q[0] = c->x[m.rm].q[1];
        break;
    case 0x13: if (!m.ismem) return -1; *(u64u*) m.ea = d->q[0]; break;
    case 0x16:
        if (pf == 3) { SRC(16); d->d[0] = d->d[1] = s->d[1]; d->d[2] = d->d[3] = s->d[3]; }       /* movshdup */
        else if (m.ismem) d->q[1] = *(u64u*) m.ea; else d->q[1] = c->x[m.rm].q[0];
        break;
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
    case 0xD6:
        if (pf == 3) { if (m.ismem) return -1; d->q[0] = c->mm[m.rm & 7]; d->q[1] = 0; break; }       /* movq2dq xmm, mm */
        if (pf == 2) { if (m.ismem) return -1; c->mm[m.reg & 7] = c->x[m.rm].q[0]; break; }         /* movdq2q mm, xmm */
        if (pf != 1) return -1;
        if (m.ismem) *(u64u*) m.ea = d->q[0]; else { c->x[m.rm].q[0] = d->q[0]; c->x[m.rm].q[1] = 0; }
        break;
    case 0x6E: if (pf != 1) return -1; d->q[0] = rdm(c, in, &m, in->w ? 8 : 4); d->q[1] = 0; break;
    case 0x7E:
        if (pf == 1) wrm(c, in, &m, in->w ? 8 : 4, d->q[0]);
        else if (pf == 3) { SRC(8); d->q[0] = s->q[0]; d->q[1] = 0; }
        else return -1;
        break;
    case 0x52: case 0x53:                                         /* rsqrtps/ss, rcpps/ss: computed exactly (hardware gives a 12-bit estimate) */
        if (pf == 0) { SRC(16); for (int i = 0; i < 4; i++) d->f[i] = op == 0x52 ? 1.0f / sqrtf(s->f[i]) : 1.0f / s->f[i]; }
        else if (pf == 3) { SRC(4); d->f[0] = op == 0x52 ? 1.0f / sqrtf(s->f[0]) : 1.0f / s->f[0]; }
        else return -1;
        break;
    case 0x7C: case 0x7D: {                                       /* SSE3 haddpd/hsubpd (66), haddps/hsubps (F2) */
        if (pf != 1 && pf != 2) return -1;
        SRC(16); V t = *d;
        if (pf == 2) { float a[4] = {t.f[0], t.f[2], s->f[0], s->f[2]}, b[4] = {t.f[1], t.f[3], s->f[1], s->f[3]};
                       for (int i = 0; i < 4; i++) d->f[i] = op == 0x7C ? a[i] + b[i] : a[i] - b[i]; }
        else { double a[2] = {t.df[0], s->df[0]}, b[2] = {t.df[1], s->df[1]};
               for (int i = 0; i < 2; i++) d->df[i] = op == 0x7C ? a[i] + b[i] : a[i] - b[i]; }
        break; }
    case 0xD0: {                                                  /* SSE3 addsubpd (66), addsubps (F2) */
        if (pf != 1 && pf != 2) return -1;
        SRC(16); V t = *d;
        if (pf == 2) { for (int i = 0; i < 4; i++) d->f[i] = (i & 1) ? t.f[i] + s->f[i] : t.f[i] - s->f[i]; }
        else { d->df[0] = t.df[0] - s->df[0]; d->df[1] = t.df[1] + s->df[1]; }
        break; }
    case 0xF0: if (pf != 2 || !m.ismem) return -1; SRC(16); *d = *s; break;       /* lddqu */
    case 0xF7: {                                                  /* maskmovdqu: byte-masked store to [rdi] */
        if (pf != 1 || m.ismem) return -1;
        V*mk = &c->x[m.rm]; u8*dst = (u8*)(c->r[RDI] + in->seg);
        for (int i = 0; i < 16; i++) if (mk->b[i] & 0x80) dst[i] = d->b[i];
        break; }
    case 0x77: break;
    default:
        if (pf != 1) return -1;
        SRC(16);
        pint(op, d, s, &ok);
        if (!ok) return -1;
    }
    return 0;
}


/* ------------------------------------------------------------ SSSE3 / SSE4.1 / SSE4.2 (0F 38 xx, 0F 3A xx)
 * The XMM forms are 66-prefixed; the SSSE3 ones also exist without a prefix on mm registers (mmx38), plus crc32 (F2 0F 38 F0/F1). */
static inline s16 sat16(s32 v) { return (s16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
static inline u16 satu16(s32 v) { return (u16)(v > 65535 ? 65535 : v < 0 ? 0 : v); }
static inline float rnd_f(float v, int md) { switch (md & 3) { case 0: return nearbyintf(v); case 1: return floorf(v); case 2: return ceilf(v); default: return truncf(v); } }
static inline double rnd_d(double v, int md) { switch (md & 3) { case 0: return nearbyint(v); case 1: return floor(v); case 2: return ceil(v); default: return trunc(v); } }

/* PCMP{E,I}STR{I,M} (SDM vol. 2B "Intel SSE4.2 string and text instructions"). a = first operand (xmm1, its length is la),
 * b = second operand (xmm2/m128, length lb). Returns IntRes2 and sets CF/ZF/SF/OF. */
static int pc_elem(const V*r, int imm, int i) {
    switch (imm & 3) { case 0: return r->b[i]; case 1: return r->w[i]; case 2: return r->sb[i]; default: return r->sw[i]; }
}
static int pc_bool(const V*a, const V*b, int imm, int la, int lb, int agg, int j, int i) {     /* BoolRes[j][i]: b[j] against a[i] */
    int va = i < la, vb = j < lb;
    if (agg == 2) { if (!va && !vb) return 1; if (!va || !vb) return 0; }          /* equal each: both invalid -> true, one invalid -> false */
    else if (agg == 3) { if (!va) return 1; if (!vb) return 0; }                    /* equal ordered: invalid needle element -> true */
    else if (!va || !vb) return 0;                                                  /* equal any, ranges */
    int x = pc_elem(a, imm, i), y = pc_elem(b, imm, j);
    if (agg == 1) return (i & 1) ? y <= x : y >= x;                                 /* ranges: even = lower bound, odd = upper bound */
    return x == y;
}
static unsigned pcmpstr(CPU*c, const V*a, const V*b, int imm, int la, int lb) {
    int n = (imm & 1) ? 8 : 16, agg = (imm >> 2) & 3, pol = (imm >> 4) & 3;
    unsigned r1 = 0, all = (1u << n) - 1, r2;
    for (int j = 0; j < n; j++) {
        int v = 0;
        switch (agg) {
        case 0: for (int i = 0; i < n && !v; i++) v = pc_bool(a, b, imm, la, lb, agg, j, i); break;
        case 1: for (int i = 0; i < n && !v; i += 2) v = pc_bool(a, b, imm, la, lb, agg, j, i) && pc_bool(a, b, imm, la, lb, agg, j, i + 1); break;
        case 2: v = pc_bool(a, b, imm, la, lb, agg, j, j); break;
        default: v = 1; for (int k = 0; j + k < n && v; k++) v = pc_bool(a, b, imm, la, lb, agg, j + k, k); break;
        }
        r1 |= (unsigned) v << j;
    }
    r2 = r1;
    if (pol == 1) r2 = ~r1 & all;
    else if (pol == 3) for (int j = 0; j < lb && j < n; j++) r2 ^= 1u << j;           /* masked negative: only valid elements of b are inverted */
    RF(c) &= ~(u64)(OF_ | SF_ | AF_ | ZF_ | PF_ | CF_);
    if (r2) RF(c) |= CF_;
    if (lb < n) RF(c) |= ZF_;
    if (la < n) RF(c) |= SF_;
    if (r2 & 1) RF(c) |= OF_;
    return r2;
}
static int pc_ilen(const V*r, int imm) { int n = (imm & 1) ? 8 : 16, k = 0; while (k < n && ((imm & 1) ? r->w[k] : r->b[k])) k++; return k; }
static int pc_elen(CPU*c, int reg, int w, int imm) {
    int n = (imm & 1) ? 8 : 16; s64 v = w ? (s64) c->r[reg] : (s64)(s32) c->r[reg];
    if (v < 0) v = v == INT64_MIN ? INT64_MAX : -v;
    return v > n ? n : (int) v;
}

static int mmx38(CPU*c, I*in, int op, int is3a) {               /* SSSE3 on mm registers: pshufb, phadd*, phsub*, pmaddubsw, psign*, pmulhrsw, pabs*, palignr */
    if (is3a ? op != 0x0F : !((op <= 0x0B) || (op >= 0x1C && op <= 0x1E))) return -1;
    M m; int imm8 = 0;
    modrm(c, in, &m, is3a);
    if (is3a) imm8 = *in->ip++;
    u64*d = &c->mm[m.reg & 7];
    V t = {{0}}, s = {{0}}, r = {{0}}; t.q[0] = *d; s.q[0] = m.ismem ? *(u64u*) m.ea : c->mm[m.rm & 7];
    if (is3a) {                                                  /* palignr: (dest:src) >> imm8 bytes */
        u8 cat[16]; memcpy(cat, s.b, 8); memcpy(cat + 8, t.b, 8);
        for (int i = 0; i < 8; i++) { int j = i + imm8; r.b[i] = j < 16 ? cat[j] : 0; }
        *d = r.q[0]; return 0;
    }
    switch (op) {
    case 0x00: for (int i = 0; i < 8; i++) r.b[i] = (s.b[i] & 0x80) ? 0 : t.b[s.b[i] & 7]; break;                                     /* pshufb */
    case 0x01: case 0x03: case 0x05: case 0x07: {                                                                                     /* phaddw/phaddsw/phsubw/phsubsw */
        int sub = op >= 5; s16 a[4] = {t.sw[0], t.sw[2], s.sw[0], s.sw[2]}, b[4] = {t.sw[1], t.sw[3], s.sw[1], s.sw[3]};
        for (int i = 0; i < 4; i++) { s32 v = sub ? a[i] - b[i] : a[i] + b[i]; r.sw[i] = (op == 0x03 || op == 0x07) ? sat16(v) : (s16) v; }
        break; }
    case 0x02: case 0x06: { int sub = op >= 5; u32 a[2] = {t.d[0], s.d[0]}, b[2] = {t.d[1], s.d[1]};                                   /* phaddd/phsubd */
        for (int i = 0; i < 2; i++) r.d[i] = sub ? a[i] - b[i] : a[i] + b[i];
        break; }
    case 0x04: for (int i = 0; i < 4; i++) r.sw[i] = sat16((s32) t.b[2 * i] * s.sb[2 * i] + (s32) t.b[2 * i + 1] * s.sb[2 * i + 1]); break;   /* pmaddubsw */
    case 0x08: for (int i = 0; i < 8; i++) r.sb[i] = s.sb[i] < 0 ? (s8)(0 - t.sb[i]) : s.sb[i] == 0 ? 0 : t.sb[i]; break;             /* psignb/w/d */
    case 0x09: for (int i = 0; i < 4; i++) r.sw[i] = s.sw[i] < 0 ? (s16)(0 - t.sw[i]) : s.sw[i] == 0 ? 0 : t.sw[i]; break;
    case 0x0A: for (int i = 0; i < 2; i++) r.sd[i] = s.sd[i] < 0 ? (s32)(0u - t.d[i]) : s.sd[i] == 0 ? 0 : t.sd[i]; break;
    case 0x0B: for (int i = 0; i < 4; i++) r.sw[i] = (s16)(((((s32) t.sw[i] * s.sw[i]) >> 14) + 1) >> 1); break;                      /* pmulhrsw */
    case 0x1C: for (int i = 0; i < 8; i++) r.b[i] = (u8)(s.sb[i] < 0 ? 0 - s.sb[i] : s.sb[i]); break;                                 /* pabsb/w/d */
    case 0x1D: for (int i = 0; i < 4; i++) r.w[i] = (u16)(s.sw[i] < 0 ? 0 - s.sw[i] : s.sw[i]); break;
    default:   for (int i = 0; i < 2; i++) r.d[i] = s.sd[i] < 0 ? 0u - s.d[i] : s.d[i]; break;
    }
    *d = r.q[0]; return 0;
}
static int sse38(CPU*c, I*in, int op, int is3a) {
    M m; V sv, *s = NULL; int imm8 = 0;
    if (!is3a && in->repne) {                                       /* crc32 (CRC-32C), F2 [66|REX.W] 0F 38 F0/F1 */
        if (op != 0xF0 && op != 0xF1) return -1;
        modrm(c, in, &m, 0);
        int sz = op == 0xF0 ? 1 : in->w ? 8 : in->p66 ? 2 : 4;
        u64 v = rdm(c, in, &m, sz); u32 crc = (u32) c->r[m.reg];
        for (int i = 0; i < sz; i++) { crc ^= (u8)(v >> (8 * i)); for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0x82F63B78u & (0u - (crc & 1))); }
        c->r[m.reg] = crc;
        return 0;
    }
    if (!in->p66) return in->rep ? -1 : mmx38(c, in, op, is3a);
    modrm(c, in, &m, is3a ? 1 : 0);
    if (is3a) imm8 = *in->ip++;
    V*d = &c->x[m.reg], t = *d;
    if (!is3a) {
        int pm = (op >= 0x20 && op <= 0x25) || (op >= 0x30 && op <= 0x35);
        if (!pm) SRC(16);
        switch (op) {
        case 0x00: for (int i = 0; i < 16; i++) d->b[i] = (s->b[i] & 0x80) ? 0 : t.b[s->b[i] & 15]; break;                      /* pshufb */
        case 0x01: case 0x02: case 0x03: case 0x05: case 0x06: case 0x07: {                                                    /* phadd/phsub w, d, sw */
            int sub = op >= 5;
            if (op == 0x02 || op == 0x06) {
                u32 a[4] = {t.d[0], t.d[2], s->d[0], s->d[2]}, b[4] = {t.d[1], t.d[3], s->d[1], s->d[3]};
                for (int i = 0; i < 4; i++) d->d[i] = sub ? a[i] - b[i] : a[i] + b[i];
            } else {
                s16 a[8] = {t.sw[0], t.sw[2], t.sw[4], t.sw[6], s->sw[0], s->sw[2], s->sw[4], s->sw[6]};
                s16 b[8] = {t.sw[1], t.sw[3], t.sw[5], t.sw[7], s->sw[1], s->sw[3], s->sw[5], s->sw[7]};
                for (int i = 0; i < 8; i++) { s32 r = sub ? a[i] - b[i] : a[i] + b[i]; d->sw[i] = (op == 0x03 || op == 0x07) ? sat16(r) : (s16) r; }
            }
            break; }
        case 0x04: for (int i = 0; i < 8; i++) d->sw[i] = sat16((s32) t.b[2 * i] * s->sb[2 * i] + (s32) t.b[2 * i + 1] * s->sb[2 * i + 1]); break;   /* pmaddubsw */
        case 0x08: for (int i = 0; i < 16; i++) d->sb[i] = s->sb[i] < 0 ? (s8)(0 - t.sb[i]) : s->sb[i] == 0 ? 0 : t.sb[i]; break;                /* psignb/w/d */
        case 0x09: for (int i = 0; i < 8; i++) d->sw[i] = s->sw[i] < 0 ? (s16)(0 - t.sw[i]) : s->sw[i] == 0 ? 0 : t.sw[i]; break;
        case 0x0A: for (int i = 0; i < 4; i++) d->sd[i] = s->sd[i] < 0 ? (s32)(0u - t.d[i]) : s->sd[i] == 0 ? 0 : t.sd[i]; break;
        case 0x0B: for (int i = 0; i < 8; i++) d->sw[i] = (s16)(((((s32) t.sw[i] * s->sw[i]) >> 14) + 1) >> 1); break;                              /* pmulhrsw */
        case 0x10: for (int i = 0; i < 16; i++) if (c->x[0].b[i] & 0x80) d->b[i] = s->b[i]; break;                                                /* pblendvb */
        case 0x14: for (int i = 0; i < 4; i++) if (c->x[0].d[i] >> 31) d->d[i] = s->d[i]; break;                                                  /* blendvps */
        case 0x15: for (int i = 0; i < 2; i++) if (c->x[0].q[i] >> 63) d->q[i] = s->q[i]; break;                                                  /* blendvpd */
        case 0x17: {                                                                                                                              /* ptest */
            int zf = !((t.q[0] & s->q[0]) | (t.q[1] & s->q[1])), cf = !((~t.q[0] & s->q[0]) | (~t.q[1] & s->q[1]));
            RF(c) &= ~(u64)(OF_ | SF_ | AF_ | ZF_ | PF_ | CF_);
            if (zf) RF(c) |= ZF_;
            if (cf) RF(c) |= CF_;
            break; }
        case 0x1C: for (int i = 0; i < 16; i++) d->b[i] = (u8)(s->sb[i] < 0 ? 0 - s->sb[i] : s->sb[i]); break;                                     /* pabsb/w/d */
        case 0x1D: for (int i = 0; i < 8; i++) d->w[i] = (u16)(s->sw[i] < 0 ? 0 - s->sw[i] : s->sw[i]); break;
        case 0x1E: for (int i = 0; i < 4; i++) d->d[i] = s->sd[i] < 0 ? 0u - s->d[i] : s->d[i]; break;
        case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25:                                                                         /* pmovsx*, pmovzx* */
        case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: {
            static const u8 esz[6] = {1, 1, 1, 2, 2, 4}, dsz[6] = {2, 4, 8, 4, 8, 8};
            int k = op & 15, es = esz[k], ds = dsz[k], cnt = 16 / ds, zx = op >= 0x30;
            SRC(cnt * es);
            for (int i = 0; i < cnt; i++) {
                u64 v = 0; memcpy(&v, s->b + i * es, (size_t) es);
                if (!zx) v = (u64) sx(v, es);
                memcpy(d->b + i * ds, &v, (size_t) ds);
            }
            break; }
        case 0x28: d->sq[0] = (s64) t.sd[0] * s->sd[0]; d->sq[1] = (s64) t.sd[2] * s->sd[2]; break;                                               /* pmuldq */
        case 0x29: for (int i = 0; i < 2; i++) d->q[i] = t.q[i] == s->q[i] ? ~0ULL : 0; break;                                                   /* pcmpeqq */
        case 0x37: for (int i = 0; i < 2; i++) d->q[i] = t.sq[i] > s->sq[i] ? ~0ULL : 0; break;                                                   /* pcmpgtq */
        case 0x2A: if (!m.ismem) return -1; *d = *s; break;                                                                                       /* movntdqa */
        case 0x2B: for (int i = 0; i < 4; i++) { d->w[i] = satu16(t.sd[i]); d->w[4 + i] = satu16(s->sd[i]); } break;                              /* packusdw */
        case 0x38: for (int i = 0; i < 16; i++) d->sb[i] = t.sb[i] < s->sb[i] ? t.sb[i] : s->sb[i]; break;                                        /* pmins/u b,w,d */
        case 0x39: for (int i = 0; i < 4; i++) d->sd[i] = t.sd[i] < s->sd[i] ? t.sd[i] : s->sd[i]; break;
        case 0x3A: for (int i = 0; i < 8; i++) d->w[i] = t.w[i] < s->w[i] ? t.w[i] : s->w[i]; break;
        case 0x3B: for (int i = 0; i < 4; i++) d->d[i] = t.d[i] < s->d[i] ? t.d[i] : s->d[i]; break;
        case 0x3C: for (int i = 0; i < 16; i++) d->sb[i] = t.sb[i] > s->sb[i] ? t.sb[i] : s->sb[i]; break;                                        /* pmaxs/u b,w,d */
        case 0x3D: for (int i = 0; i < 4; i++) d->sd[i] = t.sd[i] > s->sd[i] ? t.sd[i] : s->sd[i]; break;
        case 0x3E: for (int i = 0; i < 8; i++) d->w[i] = t.w[i] > s->w[i] ? t.w[i] : s->w[i]; break;
        case 0x3F: for (int i = 0; i < 4; i++) d->d[i] = t.d[i] > s->d[i] ? t.d[i] : s->d[i]; break;
        case 0x40: for (int i = 0; i < 4; i++) d->d[i] = t.d[i] * s->d[i]; break;                                                                 /* pmulld */
        case 0x41: {                                                                                                                              /* phminposuw */
            u16 mn = s->w[0]; int ix = 0;
            for (int i = 1; i < 8; i++) if (s->w[i] < mn) { mn = s->w[i]; ix = i; }
            memset(d, 0, 16); d->w[0] = mn; d->w[1] = (u16) ix; break; }
        default: return -1;
        }
        return 0;
    }
    int md = (imm8 & 4) ? (int)((c->mxcsr >> 13) & 3) : (imm8 & 3);       /* round*: MXCSR.RC when imm8 bit 2 is set */
    switch (op) {
    case 0x08: SRC(16); for (int i = 0; i < 4; i++) d->f[i] = rnd_f(s->f[i], md); break;                                       /* roundps/pd/ss/sd */
    case 0x09: SRC(16); for (int i = 0; i < 2; i++) d->df[i] = rnd_d(s->df[i], md); break;
    case 0x0A: SRC(4); d->f[0] = rnd_f(s->f[0], md); break;
    case 0x0B: SRC(8); d->df[0] = rnd_d(s->df[0], md); break;
    case 0x0C: SRC(16); for (int i = 0; i < 4; i++) if ((imm8 >> i) & 1) d->d[i] = s->d[i]; break;                               /* blendps/pd, pblendw */
    case 0x0D: SRC(16); for (int i = 0; i < 2; i++) if ((imm8 >> i) & 1) d->q[i] = s->q[i]; break;
    case 0x0E: SRC(16); for (int i = 0; i < 8; i++) if ((imm8 >> i) & 1) d->w[i] = s->w[i]; break;
    case 0x0F: {                                                                                                                /* palignr: (dest:src) >> imm8 bytes */
        SRC(16); u8 cat[32]; memcpy(cat, s->b, 16); memcpy(cat + 16, t.b, 16);
        for (int i = 0; i < 16; i++) { int j = i + imm8; d->b[i] = j < 32 ? cat[j] : 0; }
        break; }
    case 0x14: case 0x15: case 0x16: case 0x17: {                                                                               /* pextrb/w/d/q, extractps */
        int sz = op == 0x14 ? 1 : op == 0x15 ? 2 : (op == 0x16 && in->w) ? 8 : 4;
        u64 v = sz == 1 ? t.b[imm8 & 15] : sz == 2 ? t.w[imm8 & 7] : sz == 8 ? t.q[imm8 & 1] : t.d[imm8 & 3];
        if (m.ismem) mwr(m.ea, v, sz); else setr(c, m.rm, sz == 8 ? 8 : 4, 0, v);
        break; }
    case 0x20: d->b[imm8 & 15] = (u8)(m.ismem ? mrd(m.ea, 1) : getr(c, m.rm, 4, 0)); break;                                      /* pinsrb */
    case 0x21: {                                                                                                                /* insertps */
        int cd = (imm8 >> 4) & 3, cs; if (m.ismem) { SRC(4); cs = 0; } else { SRC(16); cs = (imm8 >> 6) & 3; }
        d->d[cd] = s->d[cs];
        for (int i = 0; i < 4; i++) if ((imm8 >> i) & 1) d->d[i] = 0;
        break; }
    case 0x22: if (in->w) d->q[imm8 & 1] = rdm(c, in, &m, 8); else d->d[imm8 & 3] = (u32) rdm(c, in, &m, 4); break;             /* pinsrd/q */
    case 0x40: {                                                                                                                /* dpps */
        SRC(16); volatile float p[4];                                       /* volatile: no fused multiply-add, the sums must round like the hardware's */
        for (int i = 0; i < 4; i++) p[i] = (imm8 & (0x10 << i)) ? d->f[i] * s->f[i] : 0.0f;
        float lo = p[0] + p[1], hi = p[2] + p[3], sum = lo + hi;
        for (int i = 0; i < 4; i++) d->f[i] = ((imm8 >> i) & 1) ? sum : 0.0f;
        break; }
    case 0x41: {                                                                                                                /* dppd */
        SRC(16); volatile double p[2];
        for (int i = 0; i < 2; i++) p[i] = (imm8 & (0x10 << i)) ? d->df[i] * s->df[i] : 0.0;
        double sum = p[0] + p[1];
        for (int i = 0; i < 2; i++) d->df[i] = ((imm8 >> i) & 1) ? sum : 0.0;
        break; }
    case 0x42: {                                                                                                                /* mpsadbw */
        SRC(16); int so = (imm8 & 3) * 4, dof = ((imm8 >> 2) & 1) * 4;
        for (int i = 0; i < 8; i++) { int sum = 0; for (int k = 0; k < 4; k++) { int df = (int) t.b[dof + i + k] - (int) s->b[so + k]; sum += df < 0 ? -df : df; } d->w[i] = (u16) sum; }
        break; }
    case 0x60: case 0x61: case 0x62: case 0x63: {                                                                               /* pcmpestrm/i, pcmpistrm/i */
        SRC(16);
        int n = (imm8 & 1) ? 8 : 16, la, lb;
        if (op < 0x62) { la = pc_elen(c, RAX, in->w, imm8); lb = pc_elen(c, RDX, in->w, imm8); }
        else { la = pc_ilen(&t, imm8); lb = pc_ilen(s, imm8); }
        unsigned r = pcmpstr(c, &t, s, imm8, la, lb);
        if (op & 1) c->r[RCX] = !r ? (u32) n : (imm8 & 0x40) ? (u32)(31 - __builtin_clz(r)) : (u32) __builtin_ctz(r);
        else {
            V*x0 = &c->x[0]; memset(x0, 0, 16);
            if (imm8 & 0x40) { for (int j = 0; j < n; j++) if ((r >> j) & 1) { if (n == 8) x0->w[j] = 0xffff; else x0->b[j] = 0xff; } }
            else x0->q[0] = r;
        }
        break; }
    default: return -1;
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
/* FXSAVE image (512 bytes), shared by fxsave/fxrstor, the signal frame and sigreturn. Layout: 0 fcw, 2 fsw (TOP in bits 11-13),
 * 4 abridged tag word (tags are not tracked: 0), 24 mxcsr, 28 mxcsr_mask, 32 ST(0..7) at 16 bytes each in stack order, 160 xmm0-15. */
static void fx_save(CPU*c, u8*p) {
    memset(p, 0, 512);
    *(u16u*) p = c->fcw; *(u16u*)(p + 2) = (u16)((c->fsw & ~0x3800) | (c->top << 11));
    *(u32u*)(p + 24) = c->mxcsr; *(u32u*)(p + 28) = 0xffff;
    for (int i = 0; i < 8; i++) ld_to80(c->st[(c->top + i) & 7], p + 32 + 16 * i);
    memcpy(p + 160, c->x, 256);
}
static void fx_restore(CPU*c, const u8*p) {
    u16 sw = *(const u16u*)(p + 2);
    c->fcw = *(const u16u*) p; c->fsw = sw & 0x4700; c->top = (sw >> 11) & 7;
    c->mxcsr = *(const u32u*)(p + 24);
    for (int i = 0; i < 8; i++) c->st[(c->top + i) & 7] = ld_from80(p + 32 + 16 * i);
    memcpy(c->x, p + 160, 256);
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
static u64 x_tointt(long double v, int sz) {            /* fisttp: truncate toward zero, whatever the rounding mode says */
    long double r = truncl(v), lim = sz == 2 ? 32768.0L : sz == 4 ? 2147483648.0L : 9223372036854775808.0L;
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
            case 1: mwr(ea, x_tointt(ST(0), 8), 8); xpop(c); return 0;                          /* fisttp m64int (SSE3) */
            case 4: {                                                                          /* frstor: 28-byte environment, then ST(0..7) as 80-bit */
                u16 sw = (u16) mrd(ea + 4, 2);
                c->fcw = (u16) mrd(ea, 2); c->fsw = sw & 0x4700; c->top = (sw >> 11) & 7;
                for (int i = 0; i < 8; i++) ST(i) = ld_from80((u8*)(ea + 28 + 10 * (u64) i));
                return 0; }
            case 6:                                                                            /* fnsave: environment + ST(0..7), then fninit */
                memset((void*) ea, 0, 28); mwr(ea, c->fcw, 2); mwr(ea + 4, (u64)((c->fsw & ~0x3800) | (c->top << 11)), 2); mwr(ea + 8, 0xffff, 2);
                for (int i = 0; i < 8; i++) ld_to80(ST(i), (u8*)(ea + 28 + 10 * (u64) i));
                c->fcw = 0x37f; c->fsw = 0; c->top = 0;
                return 0;
            }
            return -1;
        case 0xDB:
            switch (r) {
            case 0: xpush(c, (long double)(s32) mrd(ea, 4)); return 0;
            case 2: case 3: mwr(ea, x_toint(c, ST(0), 4), 4); if (r == 3) xpop(c); return 0;
            case 5: xpush(c, ld_from80((u8*) ea)); return 0;
            case 7: ld_to80(ST(0), (u8*) ea); xpop(c); return 0;
            case 1: mwr(ea, x_tointt(ST(0), 4), 4); xpop(c); return 0;                          /* fisttp m32int (SSE3) */
            }
            return -1;
        case 0xDF:
            switch (r) {
            case 0: xpush(c, (long double)(s16) mrd(ea, 2)); return 0;
            case 2: case 3: mwr(ea, x_toint(c, ST(0), 2), 2); if (r == 3) xpop(c); return 0;
            case 5: xpush(c, (long double)(s64) mrd(ea, 8)); return 0;
            case 7: mwr(ea, x_toint(c, ST(0), 8), 8); xpop(c); return 0;
            case 1: mwr(ea, x_tointt(ST(0), 2), 2); xpop(c); return 0;                          /* fisttp m16int (SSE3) */
            case 4: {                                                                          /* fbld: 18 packed BCD digits (low nibble first), sign in bit 7 of byte 9 */
                const u8*b = (const u8*) ea; long double v = 0;
                for (int i = 8; i >= 0; i--) v = v * 100 + (b[i] >> 4) * 10 + (b[i] & 15);
                xpush(c, (b[9] & 0x80) ? -v : v); return 0; }
            case 6: {                                                                          /* fbstp */
                u8*b = (u8*) ea; long double v = xround(c, ST(0)); int neg = signbit(v) != 0;
                if (neg) v = -v;
                if (isnan(v) || v >= 1e18L) { memset(b, 0, 10); b[7] = 0xC0; b[8] = 0xFF; b[9] = 0xFF; }   /* packed-BCD indefinite */
                else { u64 n = (u64) v; for (int i = 0; i < 9; i++) { b[i] = (u8)((n % 10) | ((n / 10 % 10) << 4)); n /= 100; } b[9] = neg ? 0x80 : 0; }
                xpop(c); return 0; }
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

static u64 tsc_now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (u64) ts.tv_sec * 1000000000ULL + (u64) ts.tv_nsec; }

/* 128-bit compare-and-swap on a 16-byte aligned location (cmpxchg16b). exp[0..1] = expected (rax, rdx); on failure it receives
 * the value found. Inline assembly because gcc would call libatomic for a 16-byte __atomic builtin on aarch64. */
static int cas16(u64*p, u64*exp, u64 n0, u64 n1) {
#if defined(__aarch64__)
    u64 o0, o1; u32 st; int ok;
    __asm__ __volatile__(
        "1: ldaxp %[o0], %[o1], [%[p]]\n"
        "   cmp %[o0], %[e0]\n"
        "   ccmp %[o1], %[e1], #0, eq\n"
        "   b.ne 2f\n"
        "   stlxp %w[st], %[n0], %[n1], [%[p]]\n"
        "   cbnz %w[st], 1b\n"
        "   mov %w[ok], #1\n"
        "   dmb ish\n"
        "   b 3f\n"
        "2: clrex\n"
        "   mov %w[ok], #0\n"
        "3:\n"
        : [o0] "=&r"(o0), [o1] "=&r"(o1), [st] "=&r"(st), [ok] "=&r"(ok)
        : [p] "r"(p), [e0] "r"(exp[0]), [e1] "r"(exp[1]), [n0] "r"(n0), [n1] "r"(n1)
        : "cc", "memory");
    exp[0] = o0; exp[1] = o1;
    return ok;
#elif defined(__x86_64__)
    unsigned char ok; u64 a = exp[0], d = exp[1];
    __asm__ __volatile__("lock cmpxchg16b %[m]\n\tsetz %[ok]"
        : [m] "+m"(*(unsigned __int128*) p), "+a"(a), "+d"(d), [ok] "=q"(ok) : "b"(n0), "c"(n1) : "cc", "memory");
    exp[0] = a; exp[1] = d;
    return ok;
#else
#error "cas16: unsupported host"
#endif
}

static void cpuid(CPU*c) {
    u32 leaf = (u32) c->r[RAX], sub = (u32) c->r[RCX], a = 0, b = 0, cc = 0, d = 0;
    if (leaf == 0) { a = 4; b = 0x756e6547; d = 0x49656e69; cc = 0x6c65746e; }          /* "GenuineIntel": glibc only reads leaf 1 for known vendors */
    else if (leaf == 1) { a = 0x000106a5; b = 0x00010800;      /* family 6 model 0x1a (Nehalem): the one Intel core with exactly SSE4.2 and no AVX; glibc then picks its SSE2 unaligned memmove */
           cc = (1 << 0) | (1 << 9) | (1 << 13) | (1 << 19) | (1 << 20) | (1 << 23);     /* SSE3 SSSE3 CX16 SSE4.1 SSE4.2 POPCNT = x86-64-v2; no XSAVE/AVX */
        d = (1 << 0) | (1 << 4) | (1 << 8) | (1 << 15) | (1 << 23) | (1 << 24) | (1 << 25) | (1 << 26); }
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
    else if (leaf == 0x80000000u) { a = 0x80000008u; }
    else if (leaf == 0x80000001u) { cc = 1; d = (1 << 11) | (1 << 20) | (1 << 29); }
    else if (leaf >= 0x80000002u && leaf <= 0x80000004u) {                              /* processor brand string, 16 bytes per leaf */
        char br[48] = PEMU_BRAND; u32 w[4]; memcpy(w, br + 16 * (leaf - 0x80000002u), 16);
        a = w[0]; b = w[1]; cc = w[2]; d = w[3]; }
    else if (leaf == 0x80000006u) { cc = 0x01006040u; }                                 /* L2: 256 KB, 8-way, 64-byte lines (as leaf 4) */
    else if (leaf == 0x80000008u) { a = 0x00003028u; }                                  /* 40 bits physical, 48 virtual (as /proc/cpuinfo) */
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
        else if (b == 0xF0) in.lock = 1;
        else if (b == 0x2E || b == 0x3E || b == 0x26 || b == 0x36) { }
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
            if (f < 2 && t != 7 && ATOMIC_OK(&in, &m, sz)) { u64 s_ = getr(c, m.reg, sz, in.rex), rf0 = RF(c); ARMW(m.ea, sz, o_, (RF(c) = rf0, alu(c, t, o_, s_, sz))); }
            else if (f < 2) { u64 r = alu(c, t, rdm(c, &in, &m, sz), getr(c, m.reg, sz, in.rex), sz); if (t != 7) wrm(c, &in, &m, sz, r); }
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
        if ((m.reg & 7) != 7 && ATOMIC_OK(&in, &m, sz)) { int t_ = m.reg & 7; u64 rf0 = RF(c); ARMW(m.ea, sz, o_, (RF(c) = rf0, alu(c, t_, o_, imm, sz))); break; }
        u64 r = alu(c, m.reg & 7, rdm(c, &in, &m, sz), imm, sz);
        if ((m.reg & 7) != 7) wrm(c, &in, &m, sz, r);
        break; }
    case 0x84: case 0x85: { int sz = op & 1 ? opsz : 1; modrm(c, &in, &m, 0); f_logic(c, rdm(c, &in, &m, sz) & getr(c, m.reg, sz, in.rex), sz); break; }
    case 0x86: case 0x87: { int sz = op & 1 ? opsz : 1; modrm(c, &in, &m, 0);
        if (m.ismem && !(m.ea & ((u64) sz - 1))) { u64 b = getr(c, m.reg, sz, in.rex), a = 0; ARMW(m.ea, sz, o_, (a = o_, b)); setr(c, m.reg, sz, in.rex, a); break; }
        u64 a = rdm(c, &in, &m, sz), b = getr(c, m.reg, sz, in.rex); wrm(c, &in, &m, sz, b); setr(c, m.reg, sz, in.rex, a); break; }
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
        if (kind == 2 && isrep && !(RF(c) & DF_) && cnt) {          /* rep stos: bulk fill (a fault restarts the whole instruction, which is harmless for a fill) */
            u64 v = c->r[RAX] & mskz(sz), d = c->r[RDI];
            if (sz == 1 || !v || v == (v & 0xff) * (mskz(sz) / 0xff)) memset((void*) d, (int)(v & 0xff), cnt * (u64) sz);
            else if (sz == 2) { for (u64 i = 0; i < cnt; i++) ((u16u*) d)[i] = (u16) v; }
            else if (sz == 4) { for (u64 i = 0; i < cnt; i++) ((u32u*) d)[i] = (u32) v; }
            else { for (u64 i = 0; i < cnt; i++) ((u64u*) d)[i] = v; }
            if (__builtin_expect(__atomic_load_n(&g_mt, __ATOMIC_RELAXED), 0)) __atomic_thread_fence(__ATOMIC_RELEASE);
            c->r[RDI] += cnt * (u64) sz; c->r[RCX] = 0; break;
        }
        if (kind == 4 && in.repne && !in.rep && !(RF(c) & DF_) && sz == 1 && cnt) {      /* repne scasb (strlen/memchr idiom) */
            u8 al = (u8) c->r[RAX]; const u8 *p0 = (const u8*) c->r[RDI];
            const u8 *q = memchr(p0, al, cnt);
            u64 used = q ? (u64)(q - p0) + 1 : cnt;
            f_sub(c, al, p0[used - 1], 0, 1);                    /* flags of the last comparison, as the loop would leave them */
            c->r[RDI] += used; c->r[RCX] -= used; break;
        }
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
        case 2: if (ATOMIC_OK(&in, &m, sz)) { ARMW(m.ea, sz, o_, ~o_); break; } wrm(c, &in, &m, sz, ~v & mk); break;
        case 3: if (ATOMIC_OK(&in, &m, sz)) { u64 ov = 0; ARMW(m.ea, sz, o_, (ov = o_, f_sub(c, 0, o_, 0, sz))); RF(c) &= ~(u64)CF_; if (ov & mk) RF(c) |= CF_; break; }
                { u64 r = f_sub(c, 0, v, 0, sz); RF(c) &= ~(u64)CF_; if (v & mk) RF(c) |= CF_; wrm(c, &in, &m, sz, r); break; }
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
        if (ext < 2 && ATOMIC_OK(&in, &m, sz)) {
            u64 keep = RF(c) & CF_;
            ARMW(m.ea, sz, o_, (ext == 0 ? f_add(c, o_, 1, 0, sz) : f_sub(c, o_, 1, 0, sz)));
            RF(c) = (RF(c) & ~(u64)CF_) | keep;
        } else if (ext < 2) {
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
        case 0x31: { u64 t = tsc_now(); c->r[RAX] = (u32) t; c->r[RDX] = t >> 32; break; }
        case 0x01: {                                          /* the register forms xgetbv (0F 01 D0) and rdtscp (0F 01 F9); the rest is privileged */
            u8 b = *in.ip++;
            if (b == 0xD0) { c->r[RAX] = 3; c->r[RDX] = 0; }                                   /* XCR0 = x87 | SSE (no AVX state is emulated) */
            else if (b == 0xF9) { u64 t = tsc_now(); c->r[RAX] = (u32) t; c->r[RDX] = t >> 32; c->r[RCX] = 0; }
            else goto bad;
            break; }
        case 0xA0: case 0xA8: push(c, 0); break;              /* push fs / gs: the selector reads as 0 */
        case 0xA1: case 0xA9: (void) pop(c); break;           /* pop fs / gs: ignored (the base is set with arch_prctl) */
        case 0xC3: {                                          /* movnti */
            modrm(c, &in, &m, 0);
            if (!m.ismem) goto bad;
            mwr(m.ea, getr(c, m.reg, opsz, in.rex), opsz); break; }
        case 0xC7: {                                          /* cmpxchg8b / cmpxchg16b (REX.W) */
            modrm(c, &in, &m, 0);
            if (!m.ismem || (m.reg & 7) != 1) goto bad;       /* rdrand/rdseed are not advertised */
            int ok;
            if (in.w) {
                u64 e[2] = { c->r[RAX], c->r[RDX] };
                if (!(m.ea & 15)) ok = cas16((u64*) m.ea, e, c->r[RBX], c->r[RCX]);
                else {                                        /* real hardware raises #GP; do it non-atomically instead */
                    u64 p0 = *(u64u*) m.ea, p1 = *(u64u*)(m.ea + 8);
                    ok = p0 == e[0] && p1 == e[1];
                    if (ok) { *(u64u*) m.ea = c->r[RBX]; *(u64u*)(m.ea + 8) = c->r[RCX]; } else { e[0] = p0; e[1] = p1; }
                }
                if (!ok) { c->r[RAX] = e[0]; c->r[RDX] = e[1]; }
            } else {
                u64 old = ((u64)(u32) c->r[RDX] << 32) | (u32) c->r[RAX], nv = ((u64)(u32) c->r[RCX] << 32) | (u32) c->r[RBX];
                if (!(m.ea & 7)) ok = __atomic_compare_exchange_n((u64*) m.ea, &old, nv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
                else { u64 cur = *(u64u*) m.ea; ok = cur == old; if (ok) *(u64u*) m.ea = nv; else old = cur; }
                if (!ok) { c->r[RAX] = (u32) old; c->r[RDX] = old >> 32; }
            }
            if (ok) RF(c) |= ZF_; else RF(c) &= ~(u64) ZF_;
            break; }
        case 0x38: case 0x3A: {                               /* SSSE3 / SSE4.1 / SSE4.2 / crc32 */
            u8 o3 = *in.ip++;
            if (sse38(c, &in, o3, o2 == 0x3A) < 0) undef(c, start, o2 == 0x3A ? "0F3A" : "0F38");
            break; }
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
            if (kind && in.lock && m.ismem && !(ea & ((u64) opsz - 1))) {
                u64 bm = 1ULL << bit, ov = 0;
                ARMW(ea, opsz, o_, (ov = o_, kind == 1 ? (o_ | bm) : kind == 2 ? (o_ & ~bm) : (o_ ^ bm)));
                RF(c) &= ~(u64)CF_; if ((ov >> bit) & 1) RF(c) |= CF_;
            } else if (kind) {
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
            if (!m.ismem) { if (ext >= 5) { __atomic_thread_fence(__ATOMIC_SEQ_CST); break; } goto bad; }   /* lfence/mfence/sfence */
            if (ext == 0) fx_save(c, (u8*) m.ea);
            else if (ext == 1) fx_restore(c, (const u8*) m.ea);
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
            if (ATOMIC_OK(&in, &m, sz)) {
                u64 acc = getr(c, RAX, sz, 0), nv = getr(c, m.reg, sz, in.rex), seen = 0, mk = mskz(sz);
                ARMW(m.ea, sz, o_, (seen = o_, o_ == (acc & mk) ? nv : o_));     /* a failed compare rewrites the old value, like the hardware */
                f_sub(c, acc, seen, 0, sz);
                if ((acc & mk) != (seen & mk)) setr(c, RAX, sz, 0, seen);
                break;
            }
            u64 dv = rdm(c, &in, &m, sz), acc = getr(c, RAX, sz, 0);
            f_sub(c, acc, dv, 0, sz);
            if ((acc & mskz(sz)) == (dv & mskz(sz))) wrm(c, &in, &m, sz, getr(c, m.reg, sz, in.rex)); else setr(c, RAX, sz, 0, dv);
            break; }
        case 0xC0: case 0xC1: {
            int sz = o2 & 1 ? opsz : 1; modrm(c, &in, &m, 0);
            if (ATOMIC_OK(&in, &m, sz)) { u64 sr = getr(c, m.reg, sz, in.rex), ov = 0; ARMW(m.ea, sz, o_, (ov = o_, f_add(c, o_, sr, 0, sz))); setr(c, m.reg, sz, in.rex, ov); break; }
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
 *  - Guest rax..rdi are pinned to host x19-x25,x29 while translated code runs; the other registers stay in the CPU
 *    struct (x28 points at it). The pinned ones are written back when the code is left and around a fallback: an
 *    instruction the translator does not know is executed by the interpreter (step()) through a helper call, in the
 *    middle of a translated block, and guest state is always consistent at such a call. After a fault the pinned
 *    registers are read from the signal context (jit_fault).
 *  - Arithmetic flags stay lazy, exactly as in the interpreter (la/lb/lr/lmeta). When the next instruction of the
 *    block reads the flags (jcc, setcc, cmovcc) the host flags (NZCV) of the producing instruction are used directly
 *    (cmp + b.cond). The lazy fields are only written when the flags can still be observed (liveness within the
 *    block; flags are assumed live at a block exit) and always before anything that can
 *    fault or call a helper, so a fault or a signal sees exact flags.
 *  - Blocks are superblocks: direct jmp and forward jcc on the same page are followed (a jcc's taken path is an out-of-line
 *    side exit), and a call continues inline after the callee returns. Direct branches are chained (patched on first use);
 *    indirect ones use a per-thread direct-mapped inline cache. call = host bl + a push on the return stack buffer in the
 *    CPU struct, ret = pop + compare + host ret (inline cache on a mismatch). Pending signals are polled on backward and
 *    indirect branches and after every syscall.
 *  - Faults in translated code are mapped back to the guest rip through a host-pc -> guest-rip table.
 *  - The cache is flushed when the guest unmaps, re-protects or MAP_FIXED-overwrites pages that were translated.
 *    Pages that are writable and executable at the same time are never translated (x86 needs no explicit flush
 *    for self-modifying code), they are interpreted. Instructions that end a block and are not translated (syscall, ...) are
 *    interpreted by the dispatcher outside the cache.
 *
 * Register use of translated code:  x28 CPU*   x27 &pending word of the thread   x26 inline cache of the thread
 *   x19-x25,x29 guest rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi
 *   x9,x10,x11  flag operands a,b,result of the last flag-producing instruction (kept for the lazy spill)
 *   x12,x13     effective address scratch      x0-x8,x14,x15  scratch       x16,x17  call / spill scratch
 *
 * With -DPEMU_JIT_EMBED (x86_64 test build) the generated AArch64 code is executed by the interpreter of pemu.c
 * (see tests/), which is how the back end is tested without an AArch64 machine. */
#ifdef HAVE_JIT

#ifdef PEMU_JIT_EMBED
extern void pemu_embed_run(u64 *x, u64 pc, u64 stop);
extern u64 pemu_embed_pc(void);
extern u64 pemu_embed_x(int n);
extern void pemu_embed_clear(void);
#endif

#define JCACHE_SZ  (64u << 20)          /* must stay below 128 MB: blocks branch to each other with B imm26 */
#define JTRAMP     64                   /* words reserved at the start of the cache (entry stub, epilogue) */
#define JH_BITS    18
#define JIC_BITS   13
#define JPG_BITS   15
#define JMAXI      64                   /* guest instructions per block */
#define JMAP_CAP   (4u << 20)
#define JRWX_MAX   64
#define JBLOCK_MAX 8192                 /* words a block may need at most; the cache is flushed when less is left */

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
#define OFF_RSBI   ((u32) offsetof(CPU, rsbi))
#define OFF_RSB    ((int) offsetof(CPU, rsb))      /* must stay below 512: ldp/stp immediate */

typedef struct { u64 pc; u32 *code; } JEnt;
static u32 *jbase, *jcur, *jlim, *jenter, *jepi;
static JEnt *jh;
static u32 jhn, jmapn, jpgn, jrwxn;
static u32 *jm_off; static u64 *jm_pc, *jpg;
static u64 jrwx[JRWX_MAX][2]; static int jrwx_over;
static volatile int g_jflush_req;
/* ---- one code cache shared by all guest threads.
 *  - Translation and flushing are serialised by jmu. Looking blocks up is lock-free (a block is published into jh only
 *    when it is complete; jh entries are written code first, pc last, with release/acquire).
 *  - Every thread has its own inline cache (its x26) and its own pending-signal word (its x27), so a signal that
 *    arrives on a thread is delivered on that thread, as in the interpreter.
 *  - A flush needs every other thread out of the cache (stop-the-world): the flusher sets g_jstop and bit 0 of every
 *    thread's pending word (translated code polls that word on backward and indirect branches and leaves the cache),
 *    then waits until no thread is 'active'. A thread is active from the moment it relies on a pointer into the cache
 *    (block lookup, chain patching) until it is back in C; system calls and the interpreter run while not active, so a
 *    thread blocked in a futex never delays a flush. g_jgen counts flushes: a thread that sees a new generation clears
 *    its inline cache (and the other per-thread pointers into the old cache) before it enters the cache again. */
typedef struct JT { volatile int active; volatile u64 *pp; JEnt *ic; u32 gen; struct JT *next; } JT;
static __thread JT t_jt;
static JT *jreg;                                               /* every registered thread; protected by jmu */
static pthread_mutex_t jmu = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_jstop;
static volatile u32 g_jgen;
static int g_jmt;                                              /* the guest has more than one thread: translated code carries x86 memory-ordering barriers */
static u32 *jt_start; static u64 jt_pc;                        /* block being translated (visible to its own chained exits only) */
static unsigned long long jst_blocks, jst_flush, jst_chain, jst_ins, jst_words;
static int g_jstats;
#define JOPT_SHFL 1u                    /* flags of shift-by-immediate kept lazy instead of running the interpreter */
#define JOPT_FP   2u                    /* scalar SSE float: movss/movsd, add/sub/mul/div/sqrt ss/sd, cvtsi2ss/sd */
#define JOPT_PIN  4u                    /* guest rax..rdi live in host registers while translated code runs */
#define JOPT_SB   8u                    /* superblocks: translation continues across direct jmp and forward jcc */
#define JOPT_RSB  16u                   /* return stack buffer: call/ret are host bl/ret, checked against a shadow stack */
#define JOPT_FCMP 32u                   /* comiss/comisd/ucomiss/ucomisd with the host flags of fcmp (jb/jae/jbe/ja/jp/jnp/setcc/cmovcc) */
#define JOPT_DIV  64u                   /* 32/64-bit div and idiv with udiv/sdiv when the dividend fits and the divisor is not 0 or -1 */
#define JOPT_CC   128u                  /* jcc/setcc/cmovcc after an interpreter fallback: a flags-only helper call instead of interpreting them too */
#define JOPT_INT  256u                  /* one-operand mul/imul (flags dead), bswap, 8/16-bit shl/shr/sar (flags dead) */
static unsigned g_jopt = JOPT_SHFL | JOPT_FP | JOPT_PIN | JOPT_SB | JOPT_RSB | JOPT_FCMP | JOPT_DIV | JOPT_CC | JOPT_INT;   /* PEMU_JIT_OPT=<mask> overrides (0 = none): handy to bisect a misbehaving guest */
/* Register pinning: guest rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi (numbers 0..7) are kept in callee-saved host registers, so
 * they survive helper calls and chained block-to-block branches. The CPU struct is only written back when translated
 * code is left (jepi), around an interpreter fallback (j_pin_spill / j_pin_fill) and, after a fault, from the signal
 * context (jit_fault). r8..r15 stay in memory. */
static const u8 jpin_reg[8] = { 19, 20, 21, 22, 23, 24, 25, 29 };
static int g_jpin;
static inline int pin(int n) { return (g_jpin && n >= 0 && n < 8) ? jpin_reg[n] : -1; }
static unsigned long long jdyn[512], jst_dyn;                     /* statistics: interpreter fallbacks EXECUTED, by opcode (jfbh below counts translated sites) */
static unsigned jfbh[512], jst_fb;                               /* statistics: interpreter fallbacks by opcode (0x100 + x for 0F xx) */

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
static void a_bfi(int rd, int rn, int lsb, int width) { E(0xB3400000u | (u32)((64 - lsb) & 63) << 16 | (u32)(width - 1) << 10 | (u32) rn << 5 | (u32) rd); }   /* bfi xd, xn, #lsb, #width */
static void a_lsl(int sf, int rd, int rn, int n) { int b = sf ? 64 : 32; a_bfm(sf, 0, rd, rn, (b - n) & (b - 1), b - 1 - n); }
static void a_lsr(int sf, int rd, int rn, int n) { a_bfm(sf, 0, rd, rn, n, sf ? 63 : 31); }
static void a_asr(int sf, int rd, int rn, int n) { a_bfm(sf, 1, rd, rn, n, sf ? 63 : 31); }
static void a_sxth(int sf, int rd, int rn) { a_bfm(sf, 1, rd, rn, 0, 15); }
static void a_sxtw(int rd, int rn) { a_bfm(1, 1, rd, rn, 0, 31); }
static void a_mul(int sf, int rd, int rn, int rm) { E(SFB(sf) | 0x1B007C00u | (u32) rm << 16 | (u32) rn << 5 | (u32) rd); }
static void a_csel(int sf, int cc, int rd, int rn, int rm) { E(SFB(sf) | 0x1A800000u | (u32) rm << 16 | (u32) cc << 12 | (u32) rn << 5 | (u32) rd); }
static void a_mulh(int sgn, int rd, int rn, int rm) { E((sgn ? 0x9B407C00u : 0x9BC07C00u) | (u32) rm << 16 | (u32) rn << 5 | (u32) rd); }   /* smulh / umulh */
static void a_rev(int sf, int rd, int rn) { E((sf ? 0xDAC00C00u : 0x5AC00800u) | (u32) rn << 5 | (u32) rd); }   /* rev: byte swap */
static void a_rbit(int sf, int rd, int rn) { E(SFB(sf) | 0x5AC00000u | (u32) rn << 5 | (u32) rd); }   /* rbit */
static void a_clz(int sf, int rd, int rn) { E(SFB(sf) | 0x5AC01000u | (u32) rn << 5 | (u32) rd); }    /* clz */
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
static void a_adr_at(u32 *at, int rd, u32 *target) { s64 d = (s64)((u8*) target - (u8*) at); *at = 0x10000000u | ((u32) d & 3) << 29 | (((u32)(d >> 2)) & 0x7ffff) << 5 | (u32) rd; }
static void a_bl(u32 *t) { E(0x94000000u | ((u32)(t - jcur) & 0x3ffffff)); }
static void a_ret(void) { E(0xD65F03C0u); }
static void a_div(int sf, int sgn, int rd, int rn, int rm) { E(SFB(sf) | 0x1AC00800u | (sgn ? 0x400u : 0u) | (u32) rm << 16 | (u32) rn << 5 | (u32) rd); }       /* udiv / sdiv */
static void a_msub(int sf, int rd, int rn, int rm, int ra) { E(SFB(sf) | 0x1B008000u | (u32) rm << 16 | (u32) ra << 10 | (u32) rn << 5 | (u32) rd); }     /* rd = ra - rn * rm */
static u32 *a_b_fwd(void) { E(0x14000000u); return jcur - 1; }
static void jpatch26(u32 *at) { *at |= (u32)(jcur - at) & 0x3ffffff; }
static void f_cmp(int sz, int vn, int vm) { E((sz == 8 ? 0x1E602000u : 0x1E202000u) | (u32) vm << 16 | (u32) vn << 5); }          /* fcmp dn, dm */
static void f_tox(int sz, int rd, int vn) { E((sz == 8 ? 0x9E660000u : 0x1E260000u) | (u32) vn << 5 | (u32) rd); }                  /* fmov xd, dn / fmov wd, sn */
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
    int hb = (sz == 1 && !rex && n >= 4 && n < 8) ? pin(n - 4) : -1, h = pin(n);
    if (hb >= 0) { a_bfm(1, 0, rt, hb, 8, 15); return; }          /* ah, ch, dh, bh */
    if (h >= 0 && !(sz == 1 && !rex && n >= 4 && n < 8)) {
        switch (sz) { case 8: a_mov(rt, h); break; case 4: a_movw(rt, h); break; case 2: a_andmask(rt, h, 16); break; default: a_andmask(rt, h, 8); }
        return;
    }
    u32 off = OFF_R(n);
    switch (sz) {
    case 8: a_ldst_u(3, 1, rt, RCPU, off); break;
    case 4: a_ldst_u(2, 1, rt, RCPU, off); break;
    case 2: a_ldst_u(1, 1, rt, RCPU, off); break;
    default: if (!rex && n >= 4 && n < 8) off = OFF_R(n - 4) + 1; a_ldst_u(0, 1, rt, RCPU, off);
    }
}
static void g_st(int rt, int n, int sz, int rex) {              /* sz 4 and 8 write the whole register: rt must hold a zero-extended value */
    int hb = (sz == 1 && !rex && n >= 4 && n < 8) ? pin(n - 4) : -1, h = pin(n);
    if (hb >= 0) { a_bfi(hb, rt, 8, 8); return; }
    if (h >= 0 && !(sz == 1 && !rex && n >= 4 && n < 8)) {
        switch (sz) { case 8: case 4: if (rt != h) a_mov(h, rt); break; case 2: a_bfi(h, rt, 0, 16); break; default: a_bfi(h, rt, 0, 8); }
        return;
    }
    u32 off = OFF_R(n);
    switch (sz) {
    case 8: case 4: a_ldst_u(3, 0, rt, RCPU, off); break;
    case 2: a_ldst_u(1, 0, rt, RCPU, off); break;
    default: if (!rex && n >= 4 && n < 8) off = OFF_R(n - 4) + 1; a_ldst_u(0, 0, rt, RCPU, off);
    }
}
static void g_ldx(int rt, int n, int sz2, int sx, int dsz, int rex) {   /* low sz2 bytes of guest register n, zero- or sign-extended (to dsz) into rt */
    int lo = 0, rn = n;
    if (sz2 == 1 && !rex && n >= 4 && n < 8) { rn = n - 4; lo = 8; }
    int h = pin(rn);
    if (h < 0) {
        int size = sz2 == 1 ? 0 : sz2 == 2 ? 1 : 2;
        a_ldst_u(size, !sx ? 1 : (dsz == 8 ? 2 : 3), rt, RCPU, OFF_R(rn) + (lo ? 1u : 0u)); return;
    }
    a_bfm(sx && dsz == 8, sx, rt, h, lo, lo + sz2 * 8 - 1);
}
static void j_pin_spill(void) { if (g_jpin) { a_stp(19, 20, RCPU, (int) OFF_R(0)); a_stp(21, 22, RCPU, (int) OFF_R(2)); a_stp(23, 24, RCPU, (int) OFF_R(4)); a_stp(25, 29, RCPU, (int) OFF_R(6)); } }
static void j_pin_fill(void)  { if (g_jpin) { a_ldp(19, 20, RCPU, (int) OFF_R(0)); a_ldp(21, 22, RCPU, (int) OFF_R(2)); a_ldp(23, 24, RCPU, (int) OFF_R(4)); a_ldp(25, 29, RCPU, (int) OFF_R(6)); } }
/* x86 memory ordering (TSO) for guests with more than one thread (g_jmt): every load is followed by dmb ishld and every
 * store is preceded by dmb ishst. ishld orders the load before all later loads AND stores (load->load, load->store);
 * ishst orders all earlier stores before the store (store->store). Only store->load may be reordered, as on x86. A full
 * dmb ish before each store is not needed: it would only add the load->store order that ishld already gives, and it is
 * much more expensive. Every translated load goes through a_dmb_ld, so no load escapes the argument.
 * (ldar/stlr would be cheaper still, but they fault on unaligned addresses, which x86 guests use freely.)
 * Single-threaded guests pay nothing. */
static void a_dmb_ld(void) { if (g_jmt) E(0xD50339BFu); }               /* dmb ishld */
static void a_dmb_st(void) { if (g_jmt) E(0xD5033ABFu); }               /* dmb ishst */
static void m_ld(int sz, int sgn, int sf, int rt, int rn, int off) {      /* guest memory, [rn + off], off in -256..255 */
    int size = sz == 1 ? 0 : sz == 2 ? 1 : sz == 4 ? 2 : 3;
    a_ldst_s(size, !sgn ? 1 : (sf ? 2 : 3), rt, rn, off); a_dmb_ld();
}
#define OFF_X(n) ((u32) offsetof(CPU, x) + 16u * (u32)(n))
static void q_ldc(int vt, u32 off) { E(0x3DC00000u | ((off >> 4) & 0xfff) << 10 | (u32) RCPU << 5 | (u32) vt); }       /* ldr qt, [cpu, #off] */
static void q_stc(int vt, u32 off) { E(0x3D800000u | ((off >> 4) & 0xfff) << 10 | (u32) RCPU << 5 | (u32) vt); }       /* str qt, [cpu, #off] */
static void q_ldm(int vt, int rn, int off) { E(0x3CC00000u | ((u32) off & 0x1ff) << 12 | (u32) rn << 5 | (u32) vt); a_dmb_ld(); }  /* ldur qt, [rn, #off] */
static void q_stm(int vt, int rn, int off) { a_dmb_st(); E(0x3C800000u | ((u32) off & 0x1ff) << 12 | (u32) rn << 5 | (u32) vt); }  /* stur qt, [rn, #off] */
static void q_log(int op, int vd, int vn, int vm) {                      /* 0 eor 1 orr 2 and 3 bic: vd = vn op vm (16b) */
    static const u32 base[4] = { 0x6E201C00u, 0x4EA01C00u, 0x4E201C00u, 0x4E601C00u };
    E(base[op] | (u32) vm << 16 | (u32) vn << 5 | (u32) vd);
}
static void q_v3(int kind, int size, int vd, int vn, int vm) {          /* vd = vn op vm, lanes of 1<<size bytes (16b vector) */
    /* "three same": 0x4E200400 | U << 29 | opcode << 11; "permute" (zip): 0x4E000800 | opcode << 12 */
    static const u32 base[16] = {
        0x4E208400u, 0x6E208400u, 0x6E208C00u,                                   /* 0 add   1 sub    2 cmeq   */
        0x6E206C00u, 0x6E206400u, 0x4E206C00u, 0x4E206400u,                      /* 3 umin  4 umax   5 smin   6 smax  */
        0x4E203400u,                                                             /* 7 cmgt (signed)               */
        0x6E200C00u, 0x4E200C00u, 0x6E202C00u, 0x4E202C00u,                      /* 8 uqadd 9 sqadd 10 uqsub 11 sqsub */
        0x6E201400u, 0x4E209C00u,                                                /* 12 urhadd (pavg)  13 mul      */
        0x4E003800u, 0x4E007800u };                                              /* 14 zip1 (punpckl*) 15 zip2 (punpckh*) */
    E(base[kind] | (u32) size << 22 | (u32) vm << 16 | (u32) vn << 5 | (u32) vd);
}
static void q_dup4s(int vd, int vn, int lane) { E(0x4E040400u | (u32)(lane << 3) << 16 | (u32) vn << 5 | (u32) vd); }   /* dup vd.4s, vn.s[lane] */
/* scalar float: sz 8 = double (d regs), 4 = single (s regs). Loads/stores touch only the low sz bytes of the xmm slot. */
static void f_ldc(int sz, int vt, u32 off) { E((sz == 8 ? 0xFD400000u : 0xBD400000u) | ((off >> (sz == 8 ? 3 : 2)) & 0xfff) << 10 | (u32) RCPU << 5 | (u32) vt); }   /* ldr dt/st, [cpu, #off] */
static void f_stc(int sz, int vt, u32 off) { E((sz == 8 ? 0xFD000000u : 0xBD000000u) | ((off >> (sz == 8 ? 3 : 2)) & 0xfff) << 10 | (u32) RCPU << 5 | (u32) vt); }   /* str dt/st, [cpu, #off] */
static void f_ldm(int sz, int vt, int rn, int off) { E((sz == 8 ? 0xFC400000u : 0xBC400000u) | ((u32) off & 0x1ff) << 12 | (u32) rn << 5 | (u32) vt); a_dmb_ld(); }          /* ldur dt/st, [rn, #off] (zeroes the rest of vt) */
static void f_stm(int sz, int vt, int rn, int off) { a_dmb_st(); E((sz == 8 ? 0xFC000000u : 0xBC000000u) | ((u32) off & 0x1ff) << 12 | (u32) rn << 5 | (u32) vt); }          /* stur dt/st, [rn, #off] */
static void f_arith(int t, int sz, int vd, int vn, int vm) {             /* t: 0 fadd 1 fmul 2 fsub 3 fdiv 4 fsqrt (vd = sqrt(vm)) */
    static const u32 opc[4] = { 0x2, 0x0, 0x3, 0x1 };
    u32 ty = sz == 8 ? 1u << 22 : 0;
    if (t == 4) E(0x1E21C000u | ty | (u32) vm << 5 | (u32) vd);
    else E(0x1E200800u | ty | opc[t] << 12 | (u32) vm << 16 | (u32) vn << 5 | (u32) vd);
}
static void f_scvtf(int sz, int src64, int vd, int rn) { E(0x1E220000u | (src64 ? 0x80000000u : 0u) | (sz == 8 ? 1u << 22 : 0u) | (u32) rn << 5 | (u32) vd); }   /* scvtf dd/sd, xn/wn */
static void m_st(int sz, int rt, int rn, int off) {
    int size = sz == 1 ? 0 : sz == 2 ? 1 : sz == 4 ? 2 : 3;
    a_dmb_st(); a_ldst_s(size, 0, rt, rn, off);
}

/* ------------------------------------------------------------ block cache */
static inline u32 hpc(u64 pc, int bits) { return (u32)((pc * 0x9E3779B97F4A7C15ULL) >> (64 - bits)); }
static inline u32 icidx(u64 pc) { return (u32)((pc ^ (pc >> 13)) & ((1u << JIC_BITS) - 1)); }
static u32 *jfind(u64 pc) {
    u32 h = hpc(pc, JH_BITS);
    for (;;) {
        u64 k = __atomic_load_n(&jh[h].pc, __ATOMIC_ACQUIRE);
        if (k == pc) return jh[h].code;
        if (!k) return NULL;
        h = (h + 1) & ((1u << JH_BITS) - 1);
    }
}
static u32 *jfind_emit(u64 pc) { return pc == jt_pc && jt_start ? jt_start : jfind(pc); }      /* while translating: the block under construction is a valid target */
static void jins(u64 pc, u32 *code) {                           /* jmu held; the block is complete */
    u32 h = hpc(pc, JH_BITS);
    while (jh[h].pc) h = (h + 1) & ((1u << JH_BITS) - 1);
    jh[h].code = code; __atomic_store_n(&jh[h].pc, pc, __ATOMIC_RELEASE); jhn++;
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
static void jflush(void) {                                      /* only through jit_stw_flush(): no thread is in the cache */
    jcur = jbase + JTRAMP;
    memset(jh, 0, sizeof(JEnt) << JH_BITS); memset(jpg, 0, sizeof(u64) << JPG_BITS);
    jhn = jmapn = jpgn = 0; jst_flush++;
}
static int jit_is_rwx(u64 pc) {
    if (jrwx_over) return 1;
    u64 p = pc & ~4095ULL; u32 nr = __atomic_load_n(&jrwxn, __ATOMIC_ACQUIRE);
    for (u32 k = 0; k < nr; k++) if (p < jrwx[k][1] && p + 4096 > jrwx[k][0]) return 1;
    return 0;
}
static void jit_note_rwx(u64 start, u64 len) {                  /* jmu held */
    if (jrwxn < JRWX_MAX) { jrwx[jrwxn][0] = start; jrwx[jrwxn][1] = start + len; __atomic_store_n(&jrwxn, jrwxn + 1, __ATOMIC_RELEASE); } else jrwx_over = 1;
    if (jbase && jit_has_code(start, len)) __atomic_store_n(&g_jflush_req, 1, __ATOMIC_RELEASE);
}

/* ------------------------------------------------------------ decoder (static, no guest state) */
enum { OT_NONE, OT_REG, OT_MEM, OT_IMM };
enum { JO_FB, JO_FBT, JO_NOP, JO_MOV, JO_MOVX, JO_LEA, JO_PUSH, JO_POP, JO_ALU, JO_NOT, JO_NEG, JO_SHIFT, JO_IMUL,
       JO_SETCC, JO_CMOV, JO_CWDE, JO_CDQ, JO_LEAVE, JO_XLD, JO_XST, JO_XLOG, JO_XMOVDLD, JO_XMOVDST, JO_XVEC, JO_PMOVMSKB, JO_XSHI, JO_FMOV, JO_FARITH, JO_CVTSI2F, JO_FCMP, JO_DIV, JO_XDUP, JO_BSF, JO_MUL1, JO_BSWAP, JO_JMP, JO_JCC, JO_CALL, JO_RET, JO_JMPI, JO_CALLI };
typedef struct {
    u64 ip, imm, target; s64 disp;
    u8 len, op, sz, sz2, t, fr, fw, at, bt, ar, br, rex, lva, lvb, scale, seg, sx, cnt, nost;
    s8 base, index;
} JI;
typedef struct { int reg, rm, ismem; } JM;

static inline s32 rd32s(const u8*p) { s32 v; memcpy(&v, p, 4); return v; }
static inline s16 rd16s(const u8*p) { s16 v; memcpy(&v, p, 2); return v; }
/* JI.cnt on jcc/call/calli/nop: the superblock continues after this instruction (jcc: on the fall-through path, side exit when taken;
 * call/calli: after the callee returns, through the return stack buffer; nop: a followed backward jmp that must poll for signals) */
static inline int jterm(const JI*j) {
    if ((j->op == JO_JCC || j->op == JO_CALL || j->op == JO_CALLI) && j->cnt) return 0;
    return j->op >= JO_JMP || j->op == JO_FBT;
}

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
        int t = m.reg & 7; if (t == 2 || t == 3 || (sz < 4 && (t < 4 || !(g_jopt & JOPT_INT)))) FB();
        j->op = JO_SHIFT; j->sz = (u8) sz; j->t = (u8) t; SETRM(m, 0);
        j->cnt = op <= 0xC1 ? 0 : op <= 0xD1 ? 1 : 2;
        if (j->cnt == 0) j->imm = (u8) *p++ & (sz == 8 ? 63 : 31); else if (j->cnt == 1) j->imm = 1;
        if (j->cnt == 2 || !j->imm || t < 2 || sz < 4) j->fr = 1; else j->fw = 1;      /* 8/16-bit: fr, so the interpreter fallback sees the merged flags */
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
        jmodrm(&p, j, &m, ext < 2 ? (sz == 1 ? 1 : (opsz == 2 ? 2 : 4)) : 0);
        if (BADM(m) || (ext > 3 && !(((ext >= 6 && (g_jopt & JOPT_DIV)) || (ext <= 5 && (g_jopt & JOPT_INT))) && (sz == 4 || sz == 8)))) FB();
        j->sz = (u8) sz; SETRM(m, 0);
        if (ext >= 6) { j->op = JO_DIV; j->t = (u8) ext; break; }
        if (ext >= 4) { j->op = JO_MUL1; j->t = (u8) ext; break; }       /* mul / imul r/m: rdx:rax = rax * r/m (translated only when the flags are dead; no fr/fw: the flags the interpreter leaves undefined must flow through unchanged) */
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
            if ((o2 == 0x10 || o2 == 0x11) && pf > 1 && (g_jopt & JOPT_FP) && !(pf2 && pf3)) {      /* movss / movsd */
                jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
                j->op = JO_FMOV; j->sz2 = pf2 ? 8 : 4; j->t = o2 == 0x11;
                if (st) { SETRM(m, 0); j->bt = OT_REG; j->br = (u8) m.reg; } else { j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); }
                break;
            }
            if ((o2 == 0x10 || o2 == 0x11) ? pf > 1 : (o2 == 0x6F || o2 == 0x7F) ? pf == 0 : 0) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->op = st ? JO_XST : JO_XLD;
            if (st) { SETRM(m, 0); j->bt = OT_REG; j->br = (u8) m.reg; } else { j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); }
            break; }
        case 0x51: case 0x58: case 0x59: case 0x5C: case 0x5E:             /* sqrt/add/mul/sub/div ss|sd */
            if (!(g_jopt & JOPT_FP) || pf2 == pf3) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->op = JO_FARITH; j->sz2 = pf2 ? 8 : 4;
            j->t = o2 == 0x58 ? 0 : o2 == 0x59 ? 1 : o2 == 0x5C ? 2 : o2 == 0x5E ? 3 : 4;
            j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0x2A:                                           /* cvtsi2ss / cvtsi2sd */
            if (!(g_jopt & JOPT_FP) || pf2 == pf3) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->op = JO_CVTSI2F; j->sz2 = pf2 ? 8 : 4; j->sz = w ? 8 : 4; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0x2E: case 0x2F:                                /* ucomiss/ucomisd/comiss/comisd */
            if (!(g_jopt & JOPT_FCMP) || pf3 || pf2) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->op = JO_FCMP; j->sz2 = p66 ? 8 : 4; j->fw = 1; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0xEF: case 0xEB: case 0xDB: case 0xDF:
            if (!(p66 && !pf3 && !pf2)) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->op = JO_XLOG; j->t = o2 == 0xEF ? 0 : o2 == 0xEB ? 1 : o2 == 0xDB ? 2 : 3;
            j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0xFC: case 0xFD: case 0xFE: case 0xD4: case 0xF8: case 0xF9: case 0xFA: case 0xFB: case 0x74: case 0x75: case 0x76:
            if (!(p66 && !pf3 && !pf2)) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            j->op = JO_XVEC;
            j->t = (o2 >= 0xF8 && o2 <= 0xFB) ? 1 : (o2 >= 0xFC || o2 == 0xD4) ? 0 : 2;   /* sub / add / cmpeq */
            j->sz = (u8)(o2 == 0xD4 || o2 == 0xFB ? 3 : o2 == 0xFE || o2 == 0xFA || o2 == 0x76 ? 2 : o2 == 0xFD || o2 == 0xF9 || o2 == 0x75 ? 1 : 0);
            j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0xD5: case 0xD8: case 0xD9: case 0xDA: case 0xDC: case 0xDD: case 0xDE: case 0xE0: case 0xE3: case 0xE8: case 0xE9: case 0xEA: case 0xEC: case 0xED: case 0xEE:
        case 0x60: case 0x61: case 0x62: case 0x64: case 0x65: case 0x66: case 0x68: case 0x69: case 0x6A: case 0x6C: case 0x6D: {
            if (!(p66 && !pf3 && !pf2)) FB();
            jmodrm(&p, j, &m, 0); if (BADM(m)) FB();
            int t, sz;                                       /* NEON kind (see q_v3) and lane size */
            switch (o2) {
            case 0xD5: t = 13; sz = 1; break;                                                                         /* pmullw */
            case 0xD8: case 0xD9: t = 10; sz = o2 - 0xD8; break;                                                      /* psubusb/w */
            case 0xDC: case 0xDD: t = 8; sz = o2 - 0xDC; break;                                                       /* paddusb/w */
            case 0xE8: case 0xE9: t = 11; sz = o2 - 0xE8; break;                                                      /* psubsb/w */
            case 0xEC: case 0xED: t = 9; sz = o2 - 0xEC; break;                                                       /* paddsb/w */
            case 0xDA: t = 3; sz = 0; break;                                                                          /* pminub */
            case 0xDE: t = 4; sz = 0; break;                                                                          /* pmaxub */
            case 0xEA: t = 5; sz = 1; break;                                                                          /* pminsw */
            case 0xEE: t = 6; sz = 1; break;                                                                          /* pmaxsw */
            case 0xE0: t = 12; sz = 0; break;                                                                         /* pavgb */
            case 0xE3: t = 12; sz = 1; break;                                                                         /* pavgw */
            case 0x64: case 0x65: case 0x66: t = 7; sz = o2 - 0x64; break;                                            /* pcmpgtb/w/d */
            case 0x60: case 0x61: case 0x62: t = 14; sz = o2 - 0x60; break;                                           /* punpcklbw/wd/dq */
            case 0x6C: t = 14; sz = 3; break;                                                                         /* punpcklqdq */
            case 0x68: case 0x69: case 0x6A: t = 15; sz = o2 - 0x68; break;                                           /* punpckhbw/wd/dq */
            default: t = 15; sz = 3; break;                                                                           /* punpckhqdq */
            }
            j->op = JO_XVEC; j->t = (u8) t; j->sz = (u8) sz; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break; }
        case 0x70: {                                         /* pshufd that copies one dword to all four lanes: strchr/memchr broadcast the byte this way */
            if (!(p66 && !pf3 && !pf2)) FB();
            jmodrm(&p, j, &m, 1); if (BADM(m)) FB();
            u8 im = *p++;
            if (im != 0x00 && im != 0x55 && im != 0xAA && im != 0xFF) FB();
            j->op = JO_XDUP; j->imm = im & 3; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break; }
        case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF:
            if (!(g_jopt & JOPT_INT) || opsz == 2 || pf3 || pf2 || lock) FB();
            j->op = JO_BSWAP; j->sz = (u8) opsz; j->at = OT_REG; j->ar = (u8)((o2 & 7) | rexB); break;
        case 0xBC: case 0xBD:                                /* bsf/bsr, with F3: tzcnt/lzcnt (translated only when the flags are dead) */
            jmodrm(&p, j, &m, 0); if (BADM(m) || opsz == 2 || pf2) FB();
            j->op = JO_BSF; j->sz = (u8) opsz; j->t = (u8)(o2 & 1); j->sz2 = (u8) pf3; j->fw = 1; j->at = OT_REG; j->ar = (u8) m.reg; SETRM(m, 1); break;
        case 0xD7:
            if (!(p66 && !pf3 && !pf2)) FB();
            jmodrm(&p, j, &m, 0); if (m.ismem || lock) FB();
            j->op = JO_PMOVMSKB; j->at = OT_REG; j->ar = (u8) m.reg; j->bt = OT_REG; j->br = (u8) m.rm; break;
        case 0x73: {                                         /* psrlq/psllq/psrldq/pslldq xmm, imm8 */
            if (!(p66 && !pf3 && !pf2)) FB();
            jmodrm(&p, j, &m, 1); if (m.ismem || lock) FB();
            int ext = m.reg & 7; if (ext != 2 && ext != 3 && ext != 6 && ext != 7) FB();
            j->op = JO_XSHI; j->t = (u8) ext; j->imm = *p++; j->br = (u8) m.rm; break; }
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
static int jfb_idx(const u8 *q) {                               /* statistics: opcode of the instruction at q (0x100 + x for 0F xx) */
    while (*q == 0x66 || *q == 0xF3 || *q == 0xF2 || *q == 0x64 || *q == 0x65 || *q == 0x67 || *q == 0xF0 || (*q & 0xE7) == 0x26) q++;
    if ((*q & 0xF0) == 0x40) q++;
    return *q == 0x0F ? 256 + q[1] : *q;
}
static void jh_step(CPU *c) {
    if (__builtin_expect(g_jstats, 0)) { jdyn[jfb_idx((const u8*) c->rip)]++; jst_dyn++; }
    step(c);
}

/* condition helper: after an interpreter fallback the flags live in memory, so jcc/setcc/cmov evaluate them with this
 * cheap call (it reads only the flags: no register spill/fill, no instruction decode) instead of interpreting. */
static u64 jh_cc(CPU *c, u64 cc) { return (u64) cond(c, (int) cc); }
static void j_cc_helper(int cc) { a_mov(0, RCPU); a_movi(1, (u64) cc); j_callabs((void*) jh_cc); fl.nz = 0; fl.pend = 0; }   /* x0 = 0 or 1 */

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
    static const s8 sh_t[16] = { -1, -1, -1, -1, 0, 1, -1, -1, 4, 5, -1, -1, -1, -1, -1, -1 };   /* after shl/shr/sar only e/ne/s/ns come from the host flags */
    /* after comis*: CF = below or unordered, ZF = equal or unordered, PF = unordered, OF = SF = 0. fcmp leaves unordered V=1 N=0 Z=0 C=1, equal Z=1 C=1, less N=1,
     * greater C=1: below = lt, ae = ge, be = le, a = gt, p = vs, np = vc. e/ne/le/g also need the unordered case of ZF: not one A64 condition. */
    static const s8 fc_t[16] = { -2, -3, 11, 10, -1, -1, 13, 12, -2, -3, 6, 7, -2, -3, -1, -1 };
    int v = fl.kind == LZ_FCMP ? fc_t[x86cc] : fl.kind == LZ_SUB ? sub_t[x86cc] : fl.kind == LZ_ADD ? add_t[x86cc] : fl.kind >= LZ_SHL ? sh_t[x86cc] : log_t[x86cc];
    if (v == -2) return 2;
    if (v == -3) return 1;
    if (v < 0) return -1;
    *cc = v; return 0;
}

static void j_ea(const JI *j, int *rb, int *roff) {   /* address = x[*rb] + *roff, *roff in -256..255; uses x12, x13 (*rb may be a pinned register) */
    s64 d = j->disp;
    if (j->base < 0 && j->index < 0) {
        a_movi(12, (u64) d);
        if (j->seg) { c_ld(13, j->seg == 1 ? OFF_FS : OFF_GS); a_addsh(12, 12, 13, 0); }
        *rb = 12; *roff = 0; return;
    }
    int rbase = -1, ridx = -1, cur;
    if (j->base >= 0) { int h = pin(j->base); if (h >= 0) rbase = h; else { c_ld(12, OFF_R(j->base)); rbase = 12; } }
    if (j->index >= 0) { int h = pin(j->index); if (h >= 0) ridx = h; else { c_ld(13, OFF_R(j->index)); ridx = 13; } }
    if (rbase >= 0 && ridx >= 0) { a_addsh(12, rbase, ridx, j->scale); cur = 12; }
    else if (rbase >= 0) cur = rbase;
    else { a_lsl(1, 12, ridx, j->scale); cur = 12; }
    if (j->seg) { c_ld(13, j->seg == 1 ? OFF_FS : OFF_GS); a_addsh(12, cur, 13, 0); cur = 12; }
    if (d >= -256 && d <= 255) { *rb = cur; *roff = (int) d; return; }
    if (d > 0 && d < (1 << 24)) {
        if (d & 0xfff) { a_addi(0, 12, cur, (u32) d & 0xfff, 0); cur = 12; }
        if (d >> 12) { a_addi(0, 12, cur, (u32)(d >> 12), 1); cur = 12; }
    }
    else if (d < 0 && d > -(1 << 24)) {
        u32 mm = (u32) -d;
        if (mm & 0xfff) { a_addi(1, 12, cur, mm & 0xfff, 0); cur = 12; }
        if (mm >> 12) { a_addi(1, 12, cur, mm >> 12, 1); cur = 12; }
    }
    else { a_movi(13, (u64) d); a_addsh(12, cur, 13, 0); cur = 12; }
    *rb = cur; *roff = 0;
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
static void j_push(int rv) {                                    /* the store comes first: a fault leaves rsp unchanged */
    int hs = pin(RSP);
    if (hs >= 0) { a_dmb_st(); a_ldst_s(3, 0, rv, hs, -8); a_addi(1, hs, hs, 8, 0); return; }
    c_ld(1, OFF_R(RSP)); a_addi(1, 1, 1, 8, 0); a_dmb_st(); a_ldst_s(3, 0, rv, 1, 0); c_st(1, OFF_R(RSP));
}
static void j_pop(int rv) {
    int hs = pin(RSP);
    if (hs >= 0) { a_ldst_s(3, 1, rv, hs, 0); a_dmb_ld(); a_addi(0, hs, hs, 8, 0); return; }
    c_ld(1, OFF_R(RSP)); a_ldst_s(3, 1, rv, 1, 0); a_dmb_ld(); a_addi(0, 1, 1, 8, 0); c_st(1, OFF_R(RSP));
}

static void j_fb(const JI *j) {                       /* run one instruction through the interpreter */
    a_movi(0, j->ip); c_st(0, OFF_RIP); j_pin_spill(); a_mov(0, RCPU);
    j_callabs((void*) jh_step);
    j_pin_fill(); fl.nz = 0;
}

/* ---- block exits */
static void jx_direct(u64 from, u64 target) {
    if (target <= from) {                              /* backward branch: poll for pending signals */
        a_ldst_u(3, 1, 0, RPEND, 0);
        u32 *z = a_cbz_fwd(0, 0);
        a_movi(0, target); c_st(0, OFF_RIP); a_b(jepi);
        jpatch19(z);
    }
    u32 *tc = jfind_emit(target);
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

/* ---- deferred out-of-line exits of the block being translated (side exits of jcc, unresolved callees, indirect-call misses) */
enum { JS_SIDE, JS_CALL, JS_IND };
typedef struct { int kind; u32 *br[2]; u64 from, target; int fpend, fkind, fsz; } JStub;
static JStub jstub[2 * JMAXI]; static int jnstub;
static void jx_ret(void) {                              /* guest return address in x0: predicted by the return stack buffer, then the inline cache */
    if (!(g_jopt & JOPT_RSB)) { jx_indirect(); return; }
    a_ldst_u(3, 1, 1, RPEND, 0);
    u32 *s1 = a_cbz_fwd(1, 1);                         /* signal pending: slow path polls again and leaves */
    c_ld(2, OFF_RSBI); a_addi(1, 2, 2, 1, 0); a_andmask(2, 2, 6); c_st(2, OFF_RSBI);
    a_addsh(3, RCPU, 2, 4); a_ldp(4, 30, 3, OFF_RSB);
    a_addsub(1, 1, 1, 31, 4, 0);                       /* cmp x4, x0 */
    u32 *s2 = a_bcond_fwd(1);
    a_ret();                                           /* host ret: the CPU's own return predictor follows the bl of the call */
    jpatch19(s1); jpatch19(s2);
    jx_indirect();
}
static void jrsb_push(u64 retaddr, u32 **adrsite) {    /* uses x2-x5; *adrsite = adr of the host continuation, patched by the caller */
    c_ld(2, OFF_RSBI); a_addsh(3, RCPU, 2, 4);
    a_movi(4, retaddr); *adrsite = jcur; E(0);
    a_stp(4, 5, 3, OFF_RSB);
    a_addi(0, 2, 2, 1, 0); a_andmask(2, 2, 6); c_st(2, OFF_RSBI);
}
static void jstub_add(int kind, u32 *b0, u32 *b1, u64 from, u64 target) {
    JStub *t = &jstub[jnstub++]; t->kind = kind; t->br[0] = b0; t->br[1] = b1; t->from = from; t->target = target;
    t->fpend = fl.pend; t->fkind = fl.kind; t->fsz = fl.sz;
}
static void jstubs_emit(void) {
    for (int i = 0; i < jnstub; i++) {
        JStub *t = &jstub[i];
        if (t->kind == JS_CALL) {                       /* br[0] is a bl: becomes the chain patch site */
            u32 *site = t->br[0]; *site = 0x94000000u | ((u32)(jcur - site) & 0x3ffffff);
            a_adr(0, site); a_addi(0, 0, 0, 1, 0); c_st(0, OFF_PATCH);
            a_movi(0, t->target); c_st(0, OFF_RIP); a_b(jepi);
        } else if (t->kind == JS_IND) {
            for (int k = 0; k < 2; k++) if (t->br[k]) { u32 w = *t->br[k]; *t->br[k] = w | ((u32)(jcur - t->br[k]) & 0x7ffff) << 5; }
            c_st(0, OFF_RIP); a_b(jepi);
        } else {
            jpatch19(t->br[0]);
            if (t->fpend) { fl.kind = t->fkind; fl.sz = t->fsz; fl_spill(); }
            jx_direct(t->from, t->target);
        }
    }
    jnstub = 0;
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

static int jemit(const JI *j) {          /* returns 1 when it ended the block (the exit is emitted) */
    int sz = j->sz, rb = 0, off = 0, hm = j->at == OT_MEM || j->bt == OT_MEM, sf = sz == 8;
    switch (j->op) {
    case JO_NOP:
        if (j->cnt) {                                    /* followed backward jmp: poll for signals here, since no exit stub will */
            a_ldst_u(3, 1, 0, RPEND, 0);
            u32 *z = a_cbz_fwd(0, 0);
            if (fl.pend) fl_spill();
            a_movi(0, j->target); c_st(0, OFF_RIP); a_b(jepi);
            jpatch19(z);
        }
        break;
    case JO_FB: fl_flush(j->lvb); j_fb(j); break;
    case JO_FBT:                                         /* not translated, ends the block: the dispatcher interprets it (outside the cache, so a blocking system call never holds up a flush) */
        fl_flush(1); a_movi(0, j->ip); c_st(0, OFF_RIP); a_movi(0, 1); c_st(0, OFF_PATCH); a_b(jepi); return 1;
    case JO_MOV:
        if (hm) { fl_flush(j->lvb); j_ea(j, &rb, &off); }
        j_opld(0, j, 1, sz, rb, off); j_opst(0, j, sz, rb, off); break;
    case JO_MOVX: {
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); m_ld(j->sz2, j->sx, j->sz == 8, 0, rb, off); }
        else g_ldx(0, j->br, j->sz2, j->sx, j->sz, j->rex);
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
        if (pin(RBP) >= 0) { a_ldst_s(3, 1, 2, pin(RBP), 0); a_dmb_ld(); a_addi(0, pin(RSP), pin(RBP), 8, 0); a_mov(pin(RBP), 2); break; }
        c_ld(0, OFF_R(RBP)); a_ldst_s(3, 1, 2, 0, 0); a_dmb_ld(); a_addi(0, 0, 0, 8, 0); c_st(0, OFF_R(RSP)); c_st(2, OFF_R(RBP)); break;
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
        if (j->lva && (!(g_jopt & JOPT_SHFL) || j->cnt == 2 || !j->imm || j->t < 4 || sz < 4)) { fl_flush(j->lvb); j_fb(j); break; }   /* flags wanted: interpreter */
        if (j->lva) {                                            /* shl/shr/sar by a non-zero immediate, flags wanted: keep them lazy (x9 = value, x10 = count, x11 = result) */
            fl_flush(j->lvb); fl.nz = 0;
            if (j->at == OT_MEM) j_ea(j, &rb, &off);
            j_opld(9, j, 0, sz, rb, off);
            int n = (int) j->imm, kind;
            if (j->t == 5) { a_lsr(sf, 11, 9, n); kind = LZ_SHR; }
            else if (j->t == 7) { a_asr(sf, 11, 9, n); kind = LZ_SAR; }
            else { a_lsl(sf, 11, 9, n); kind = LZ_SHL; }
            a_movi(10, (u64) n);
            a_log(sf, 3, 0, 31, 11, 11);                         /* tst x11, x11: host Z and N now describe the result */
            j_opst(11, j, sz, rb, off);
            fl.kind = kind; fl.sz = sz; fl.nz = 1; fl.pend = 1;
            break;
        }
        fl_flush(j->lvb); fl.nz = 0;
        if (j->at == OT_MEM) j_ea(j, &rb, &off);
        if (sz < 4 && j->t == 7) {                       /* 8/16-bit sar: sign-extend the operand first (flags dead; shl/shr work on the zero-extended value) */
            if (j->at == OT_MEM) m_ld(sz, 1, 0, 0, rb, off); else g_ldx(0, j->ar, sz, 1, 4, j->rex);
        } else j_opld(0, j, 0, sz, rb, off);
        if (j->cnt == 2) {
            g_ld(1, RCX, 8, 0);
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
        if (m < 0 && !(g_jopt & JOPT_CC)) { j_fb(j); break; }
        if (m < 0) {
            fl_flush(1); j_cc_helper(j->t);
            if (j->at == OT_MEM) { j_ea(j, &rb, &off); m_st(1, 0, rb, off); } else g_st(0, j->ar, 1, j->rex);
            break;
        }
        if (m == 0) a_cset(0, cc); else a_movz(0, 0, m == 1, 0);
        if (j->at == OT_MEM) { j_ea(j, &rb, &off); m_st(1, 0, rb, off); } else g_st(0, j->ar, 1, j->rex);
        break; }
    case JO_CMOV: {
        fl_flush(j->lvb);
        int cc = 0, m = fl.nz ? jmapcc(j->t, &cc) : -1;
        if (m < 0 && !(g_jopt & JOPT_CC)) { j_fb(j); break; }
        if (m < 0) {
            fl_flush(1); j_cc_helper(j->t); a_mov(2, 0);                  /* x2 = condition; the source is read either way */
            if (j->bt == OT_MEM) { j_ea(j, &rb, &off); m_ld(sz, 0, 0, 1, rb, off); } else g_ld(1, j->br, sz, 0);
            g_ld(0, j->ar, sz, 0);
            a_addsub(1, 1, 1, 31, 2, 31); a_csel(sf, 1, 0, 1, 0);        /* subs xzr, x2, xzr; csel ne */
            g_st(0, j->ar, sz, 0); break;
        }
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
    case JO_XVEC:
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); q_ldm(1, rb, off); } else q_ldc(1, OFF_X(j->br));
        q_ldc(0, OFF_X(j->ar));
        q_v3(j->t, j->sz, 0, 0, 1);
        q_stc(0, OFF_X(j->ar)); break;
    case JO_XDUP:
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); q_ldm(1, rb, off); } else q_ldc(1, OFF_X(j->br));
        q_dup4s(0, 1, (int) j->imm);
        q_stc(0, OFF_X(j->ar)); break;
    case JO_MUL1: {                                      /* mul / imul r/m (32 or 64 bit), flags dead: rdx:rax = rax * r/m */
        if (j->lva) { fl_flush(j->lvb); j_fb(j); break; }
        fl_flush(j->lvb); fl.nz = 0;
        int sg = j->t == 5;
        if (j->at == OT_MEM) { j_ea(j, &rb, &off); m_ld(sz, 0, 0, 1, rb, off); } else g_ld(1, j->ar, sz, j->rex);
        g_ld(0, RAX, sz, 0);
        if (sz == 8) { a_mul(1, 2, 0, 1); a_mulh(sg, 3, 0, 1); }
        else {
            if (sg) { a_sxtw(0, 0); a_sxtw(1, 1); }
            a_mul(1, 2, 0, 1); a_lsr(1, 3, 2, 32); a_movw(2, 2);
        }
        g_st(2, RAX, sz, 0); g_st(3, RDX, sz, 0); break; }
    case JO_BSWAP:
        g_ld(0, j->ar, sz, 0); a_rev(sf, 0, 0); g_st(0, j->ar, sz, 0); break;
    case JO_BSF: {
        if (j->lva && j->sz2) { fl_flush(j->lvb); j_fb(j); break; }          /* tzcnt/lzcnt flags (CF, ZF of the result) are not modelled: interpreter */
        fl_flush(j->lvb); fl.nz = 0;                                         /* the compare below clobbers the host's flags; bsf defines ZF afresh */
        if (j->bt == OT_MEM) j_ea(j, &rb, &off);
        j_opld(0, j, 1, sz, rb, off);                                        /* x0 = source, zero-extended */
        if (j->t == 0) { a_rbit(sf, 2, 0); a_clz(sf, 2, 2); }                /* bsf / tzcnt: clz(rbit(x)), the operand width for 0 */
        else if (j->sz2) a_clz(sf, 2, 0);                                    /* lzcnt */
        else { a_clz(sf, 2, 0); a_movi(3, (u64)(sz * 8 - 1)); a_addsub(sf, 1, 0, 2, 3, 2); }   /* bsr: width - 1 - clz */
        if (j->sz2) g_st(2, j->ar, sz, 0);
        else {                                                               /* plain bsf/bsr leave the destination alone for a zero source */
            a_addsub(sf, 1, 1, 31, 0, 31);                                   /* cmp x0, #0 */
            g_ld(3, j->ar, 8, 0); a_csel(1, 0, 2, 3, 2); g_st(2, j->ar, 8, 0);
        }
        if (j->lva) {                                                        /* ZF = (source == 0) is what tst x,x gives; the other flags are undefined after bsf */
            a_mov(11, 0); a_log(sf, 3, 0, 31, 11, 11);
            fl.kind = LZ_LOGIC; fl.sz = sz; fl.nz = 1; fl.pend = 1;
        }
        break; }
    case JO_PMOVMSKB:                                    /* bit i of the result = top bit of byte i: (x & 0x80..80) * 0x0002040810204081 >> 56 */
        c_ld(0, OFF_X(j->br)); c_ld(1, OFF_X(j->br) + 8);
        a_movi(2, 0x8080808080808080ULL); a_movi(3, 0x0002040810204081ULL);
        a_log(1, 0, 0, 0, 0, 2); a_log(1, 0, 0, 1, 1, 2);
        a_mul(1, 0, 0, 3); a_mul(1, 1, 1, 3);
        a_lsr(1, 0, 0, 56); a_lsr(1, 1, 1, 56);
        a_logsh(1, 0, 0, 1, 0, 8);                       /* orr x0, x0, x1, lsl #8 */
        g_st(0, j->ar, 4, 0); break;
    case JO_XSHI: {                                      /* the count is known: shift the two 64-bit halves with integer code (x0 = low, x1 = high) */
        u32 o = OFF_X(j->br); unsigned n = (unsigned) j->imm;
        if (j->t == 2 || j->t == 6) {                    /* psrlq / psllq */
            if (n > 63) { c_st(31, o); c_st(31, o + 8); break; }
            if (n == 0) break;
            c_ld(0, o); c_ld(1, o + 8);
            if (j->t == 2) { a_lsr(1, 0, 0, (int) n); a_lsr(1, 1, 1, (int) n); } else { a_lsl(1, 0, 0, (int) n); a_lsl(1, 1, 1, (int) n); }
            c_st(0, o); c_st(1, o + 8); break;
        }
        if (n > 15) { c_st(31, o); c_st(31, o + 8); break; }       /* psrldq / pslldq: n bytes */
        if (n == 0) break;
        int b = (int) n * 8; c_ld(0, o); c_ld(1, o + 8);
        if (j->t == 3) {                                 /* psrldq: whole xmm >> b */
            if (n < 8) { a_lsr(1, 2, 0, b); a_logsh(1, 0, 2, 1, 0, 64 - b); a_lsr(1, 1, 1, b); c_st(0, o); c_st(1, o + 8); }
            else { if (n == 8) a_mov(0, 1); else a_lsr(1, 0, 1, b - 64); c_st(0, o); c_st(31, o + 8); }
        } else {                                         /* pslldq: whole xmm << b */
            if (n < 8) { a_lsl(1, 2, 1, b); a_logsh(1, 1, 2, 0, 1, 64 - b); a_lsl(1, 0, 0, b); c_st(0, o); c_st(1, o + 8); }
            else { if (n != 8) a_lsl(1, 0, 0, b - 64); c_st(31, o); c_st(0, o + 8); }
        }
        break; }
    case JO_XMOVDLD:
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); m_ld(sz, 0, 0, 0, rb, off); } else g_ld(0, j->br, sz, 0);
        c_st(0, OFF_X(j->ar)); c_st(31, OFF_X(j->ar) + 8); break;
    case JO_XMOVDST:
        c_ld(0, OFF_X(j->br));
        if (j->at == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); m_st(sz, 0, rb, off); }
        else { if (sz == 4) a_movw(0, 0); g_st(0, j->ar, sz, 0); }
        break;
    case JO_FMOV:                                        /* movss / movsd */
        if (!j->t) {
            if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); f_ldm(j->sz2, 0, rb, off); q_stc(0, OFF_X(j->ar)); }   /* from memory: upper bits become zero */
            else { f_ldc(j->sz2, 0, OFF_X(j->br)); f_stc(j->sz2, 0, OFF_X(j->ar)); }                                          /* reg to reg: only the low element moves */
        } else {
            f_ldc(j->sz2, 0, OFF_X(j->br));
            if (j->at == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); f_stm(j->sz2, 0, rb, off); } else f_stc(j->sz2, 0, OFF_X(j->ar));
        }
        break;
    case JO_FARITH:
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); f_ldm(j->sz2, 1, rb, off); } else f_ldc(j->sz2, 1, OFF_X(j->br));
        if (j->t != 4) f_ldc(j->sz2, 0, OFF_X(j->ar));
        f_arith(j->t, j->sz2, 0, 0, 1);
        f_stc(j->sz2, 0, OFF_X(j->ar)); break;
    case JO_CVTSI2F:
        if (j->bt == OT_MEM) { fl_flush(j->lvb); j_ea(j, &rb, &off); m_ld(sz, 0, 0, 0, rb, off); } else g_ld(0, j->br, sz, 0);
        f_scvtf(j->sz2, sz == 8, 0, 0);
        f_stc(j->sz2, 0, OFF_X(j->ar)); break;
    case JO_FCMP:
        fl_flush(j->lvb); fl.nz = 0;
        if (j->bt == OT_MEM) { j_ea(j, &rb, &off); f_ldm(j->sz2, 1, rb, off); } else f_ldc(j->sz2, 1, OFF_X(j->br));
        if (!j->lva) break;                                              /* flags dead: nothing but the (possibly faulting) load */
        f_ldc(j->sz2, 0, OFF_X(j->ar));
        f_cmp(j->sz2, 0, 1); f_tox(j->sz2, 9, 0); f_tox(j->sz2, 10, 1);   /* host flags for jcc/setcc/cmov; the operands for the lazy spill */
        fl.kind = LZ_FCMP; fl.sz = j->sz2; fl.nz = 1; fl.pend = 1;
        break;
    case JO_DIV: {                                                       /* rdx:rax / r/m, only when it cannot trap: the interpreter does the rest */
        fl_flush(j->lvb); fl.nz = 0;
        int sg = j->t == 7;
        if (j->at == OT_MEM) { j_ea(j, &rb, &off); m_ld(sz, 0, 0, 1, rb, off); } else g_ld(1, j->ar, sz, j->rex);
        u32 *sl[4]; int ns = 0;
        sl[ns++] = a_cbz_fwd(0, 1);                                      /* divisor 0 */
        g_ld(0, RAX, sz, 0); g_ld(2, RDX, sz, 0);
        if (!sg) sl[ns++] = a_cbz_fwd(1, 2);                             /* rdx != 0: wide dividend */
        else {
            a_asr(sf, 5, 0, sf ? 63 : 31); a_log(1, 2, 0, 6, 2, 5);        /* rdx must be the sign of rax */
            sl[ns++] = a_cbz_fwd(1, 6);
            a_addi(0, 5, 1, 1, 0); if (!sf) a_lsr(1, 5, 5, 32);          /* divisor -1 (INT_MIN / -1 traps) */
            sl[ns++] = a_cbz_fwd(sf ? 0 : 1, 5);
        }
        a_div(sf, sg, 3, 0, 1); a_msub(sf, 4, 3, 1, 0);
        g_st(3, RAX, sz, 0); g_st(4, RDX, sz, 0);
        u32 *done = a_b_fwd();
        for (int k = 0; k < ns; k++) jpatch19(sl[k]);
        j_fb(j);
        jpatch26(done);
        fl.nz = 0;
        break; }
    case JO_JMP: fl_flush(j->lvb); jx_direct(j->ip, j->target); return 1;
    case JO_JCC: {
        int cc = 0, m = fl.nz ? jmapcc(j->t, &cc) : -1;
        u64 next = j->ip + j->len;
        if (j->cnt) {                                    /* superblock: the fall-through path continues, the taken path is an out-of-line side exit */
            if (m < 0 && !(g_jopt & JOPT_CC)) { fl_flush(1); j_fb(j); c_ld(0, OFF_RIP); jx_indirect(); return 1; }
            if (m < 0) { fl_flush(1); j_cc_helper(j->t); jstub_add(JS_SIDE, a_cbz_fwd(1, 0), NULL, j->ip, j->target); break; }   /* flags in memory: helper decides, cbnz to the side exit */
            if (m == 1) { fl_flush(1); jx_direct(j->ip, j->target); return 1; }
            if (m == 2) break;
            jstub_add(JS_SIDE, a_bcond_fwd(cc), NULL, j->ip, j->target);       /* the stub is entered by the branch itself: x9-x11 still hold the lazy flags */
            break;
        }
        fl_flush(1);
        if (m < 0 && !(g_jopt & JOPT_CC)) { j_fb(j); c_ld(0, OFF_RIP); jx_indirect(); }
        else if (m < 0) { j_cc_helper(j->t); u32 *bt = a_cbz_fwd(1, 0); jx_direct(j->ip, next); jpatch19(bt); jx_direct(j->ip, j->target); }   /* flags in memory: helper decides, both exits chain */
        else if (m == 1) jx_direct(j->ip, j->target);
        else if (m == 2) jx_direct(j->ip, next);
        else { u32 *bt = a_bcond_fwd(cc); jx_direct(j->ip, next); jpatch19(bt); jx_direct(j->ip, j->target); }
        return 1; }
    case JO_CALL:
        fl_flush(j->lvb);
        a_movi(0, j->imm); j_push(0);
        if (!j->cnt) { jx_direct(j->ip, j->target); return 1; }
        else {                                           /* bl callee; the code after it is the continuation, reached by the callee's ret */
            u32 *adr; jrsb_push(j->imm, &adr);
            u32 *tc = jfind_emit(j->target);
            if (tc) a_bl(tc); else { E(0x94000000u); jstub_add(JS_CALL, jcur - 1, NULL, j->ip, j->target); }
            a_adr_at(adr, 5, jcur);
            fl.nz = 0; fl.pend = 0;
        }
        break;
    case JO_RET:
        fl_flush(j->lvb);
        if (pin(RSP) >= 0) {
            int hs = pin(RSP);
            a_ldst_s(3, 1, 0, hs, 0); a_dmb_ld(); a_addi(0, hs, hs, 8, 0);
            if (j->imm) { a_movi(2, j->imm); a_addsh(hs, hs, 2, 0); }
            jx_ret(); return 1;
        }
        c_ld(1, OFF_R(RSP)); a_ldst_s(3, 1, 0, 1, 0); a_dmb_ld(); a_addi(0, 1, 1, 8, 0);
        if (j->imm) { a_movi(2, j->imm); a_addsh(1, 1, 2, 0); }
        c_st(1, OFF_R(RSP)); jx_ret(); return 1;
    case JO_JMPI: case JO_CALLI:
        fl_flush(j->lvb);
        if (j->at == OT_MEM) { j_ea(j, &rb, &off); m_ld(8, 0, 0, 0, rb, off); } else g_ld(0, j->ar, 8, 0);
        if (j->op == JO_CALLI) { a_movi(2, j->imm); j_push(2); }
        if (!j->cnt) { jx_indirect(); return 1; }
        {                                                /* indirect call: inline cache hit = blr, miss = leave through an out-of-line exit */
            u32 *adr; jrsb_push(j->imm, &adr);
            a_ldst_u(3, 1, 1, RPEND, 0);
            u32 *s1 = a_cbz_fwd(1, 1);
            a_logsh(2, 2, 0, 0, 1, 13); a_andmask(2, 2, JIC_BITS); a_addsh(2, RIC, 2, 4);
            a_ldp(3, 4, 2, 0); a_addsub(1, 1, 1, 31, 3, 0);
            u32 *s2 = a_bcond_fwd(1);
            E(0xD63F0000u | 4u << 5);                    /* blr x4 */
            a_adr_at(adr, 5, jcur);
            jstub_add(JS_IND, s1, s2, j->ip, 0);
            fl.nz = 0; fl.pend = 0;
        }
        break;
    }
    return 0;
}

/* ------------------------------------------------------------ translation, dispatch, hooks */
static void jit_stw_locked(void) {                       /* flush the whole cache. jmu held, the caller is not active */
    JT *me = &t_jt;
    __atomic_store_n(&g_jstop, 1, __ATOMIC_SEQ_CST);
    for (JT *t = jreg; t; t = t->next) if (t != me) __atomic_or_fetch((u64*) t->pp, 1ULL, __ATOMIC_SEQ_CST);     /* bit 0: leave the cache (no signal has number 0) */
    for (JT *t = jreg; t; t = t->next) if (t != me) while (__atomic_load_n(&t->active, __ATOMIC_SEQ_CST)) sched_yield();
    jflush();
    __atomic_add_fetch(&g_jgen, 1, __ATOMIC_SEQ_CST);
    __atomic_store_n(&g_jstop, 0, __ATOMIC_SEQ_CST);
}
static void jit_flush_req(void) {
    pthread_mutex_lock(&jmu);
    if (__atomic_exchange_n(&g_jflush_req, 0, __ATOMIC_ACQ_REL)) jit_stw_locked();
    pthread_mutex_unlock(&jmu);
}

static void jtranslate(u64 pc) {                          /* the caller is not active; the block is found with jfind() afterwards */
    JI ji[JMAXI]; int n = 0; u64 ip = pc;
    int sb = !!(g_jopt & JOPT_SB), rsb = !!(g_jopt & JOPT_RSB);
    while (n < JMAXI) {                                   /* decoding reads guest code but touches no JIT state: a fault here is the guest's fault, and no lock is held */
        if (n && (ip & 0xfff) > 4096 - 16) break;               /* an instruction may cross into a page we have not looked at */
        JI *j = &ji[n];
        if (!jdecode(j, ip)) { memset(j, 0, sizeof *j); j->ip = ip; j->op = JO_FBT; j->fr = j->fw = 1; n++; break; }
        n++;
        /* Superblock: keep translating along the likely path. Only addresses on pages this block already reads are followed, so that
         * decoding never touches a page the guest has not reached yet (it could be unmapped: the fault belongs to the guest, later). */
        if (j->op == JO_JMP && sb && n < JMAXI && (j->target & 0xfff) <= 4096 - 16) {
            int ok = 0;
            for (int k = 0; k < n; k++) { if (ji[k].ip == j->target) { ok = 0; break; } if ((ji[k].ip >> 12) == (j->target >> 12)) ok = 1; }
            if (ok) { j->op = JO_NOP; j->cnt = j->target <= j->ip; ip = j->target; continue; }
        }
        if (j->op == JO_JCC && sb && n < JMAXI && j->target > j->ip) { j->cnt = 1; ip = j->ip + j->len; continue; }       /* forward: assume not taken */
        if ((j->op == JO_CALL || j->op == JO_CALLI) && rsb && n < JMAXI) {                                                /* the callee returns here through the return stack buffer */
#ifdef PEMU_JIT_ABI_FLAGS
            j->fw = 1;
#else
            j->fr = 1;
#endif
            j->cnt = 1; ip = j->ip + j->len; continue;
        }
        ip += j->len;
        if (jterm(j)) break;
    }
    u64 endip = ip;
    pthread_mutex_lock(&jmu);
    if (jfind(pc)) { pthread_mutex_unlock(&jmu); return; }       /* another thread was faster */
    if ((u64)(jlim - jcur) < 2 * JBLOCK_MAX || jmapn + 4 * JMAXI > JMAP_CAP || jhn * 2 > (1u << JH_BITS)) jit_stw_locked();
    if (g_jstats)
        for (int i = 0; i < n; i++)
            if (ji[i].op == JO_FB || ji[i].op == JO_FBT) {
                jfbh[jfb_idx((const u8*) ji[i].ip)]++; jst_fb++;
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
    jt_start = start; jt_pc = pc;
    memset(&fl, 0, sizeof fl);
    int ended = 0; jnstub = 0;
    for (int i = 0; i < n && !ended; i++) {
        u32 k = jmapn; jm_off[k] = (u32)((u8*) jcur - (u8*) jbase); jm_pc[k] = ji[i].ip;
        __atomic_store_n(&jmapn, k + 1, __ATOMIC_RELEASE);          /* the entry is complete before the fault handler of another thread can see it */
        ended = jemit(&ji[i]);
    }
    if (!ended) { fl_flush(1); jx_direct(ji[n - 1].ip, endip); }
    jstubs_emit();
    jst_blocks++; jst_ins += (unsigned) n; jst_words += (unsigned long long)(jcur - start);
    j_sync_icache(start, jcur);
    jt_start = NULL; jt_pc = 0;
    jins(pc, start);                                              /* published: other threads may run it from now on */
    pthread_mutex_unlock(&jmu);
}

#define JSTOP 0x1000ULL
#ifdef PEMU_JIT_EMBED
static __thread u64 jemb_stack[8192];
#endif
static void jit_enter(CPU *c, u32 *code) {
    JT *t = &t_jt;
#ifdef PEMU_JIT_EMBED
    u64 x[32] = {0};
    x[0] = (u64) c; x[1] = (u64) code; x[2] = (u64)(void*) t->pp; x[3] = (u64) t->ic; x[30] = JSTOP; x[31] = (u64)(jemb_stack + 8192);
    pemu_embed_run(x, (u64) jenter, JSTOP);
#else
    ((void (*)(CPU*, u32*, volatile u64*, JEnt*)) (void*) jenter)(c, code, t->pp, t->ic);
#endif
}

/* A thread that has seen a flush starts again with an empty inline cache and no pending chain patch. */
static void jit_rsb_clear(CPU *c) { c->rsbi = 0; for (int i = 0; i < RSB_N; i++) { c->rsb[i][0] = ~0ULL; c->rsb[i][1] = 0; } }
static void jit_resync(JT *t, CPU *c) {
    memset(t->ic, 0, sizeof(JEnt) << JIC_BITS);
    c->patch = 0; jit_rsb_clear(c);
    t->gen = g_jgen;
#ifndef PEMU_JIT_EMBED
    __asm__ volatile("isb" ::: "memory");
#endif
}
static inline void jreg_enter(JT *t, CPU *c) {
    for (;;) {
        __atomic_store_n(&t->active, 1, __ATOMIC_SEQ_CST);
        if (!__atomic_load_n(&g_jstop, __ATOMIC_SEQ_CST)) break;
        __atomic_store_n(&t->active, 0, __ATOMIC_SEQ_CST);      /* a flush is running: wait for it */
        pthread_mutex_lock(&jmu); pthread_mutex_unlock(&jmu);
    }
    if (__builtin_expect(t->gen != __atomic_load_n(&g_jgen, __ATOMIC_ACQUIRE), 0)) jit_resync(t, c);
}
static inline void jreg_leave(JT *t) { __atomic_store_n(&t->active, 0, __ATOMIC_SEQ_CST); }

static void jit_dispatch(CPU *c) {
    JT *t = &t_jt;
    for (;;) {
        if (__builtin_expect(__atomic_load_n(&g_jflush_req, __ATOMIC_ACQUIRE), 0)) jit_flush_req();
        u64 pc = c->rip;
        if (c->patch == 1) {                                  /* the block ended with an instruction the translator does not handle: interpret it here */
            c->patch = 0;
            if (__builtin_expect(g_jstats, 0)) { jdyn[jfb_idx((const u8*) pc)]++; jst_dyn++; }
            step(c); return;
        }
        jreg_enter(t, c);
        u32 *code = jfind(pc);
        if (!code) {
            jreg_leave(t);
            if ((pc & 0xfff) > 4096 - 16 || jit_is_rwx(pc)) { c->patch = 0; step(c); return; }     /* interpret: see header comment */
            jtranslate(pc);
            continue;
        }
        if (c->patch) {                                       /* chain: turn the exit's b (or bl, bit 0) into a branch to the new code */
            u64 pv = c->patch; u32 *site = (u32*)(pv & ~3ULL);
            *site = ((pv & 1) ? 0x94000000u : 0x14000000u) | ((u32)(code - site) & 0x3ffffff);
            j_sync_icache(site, site + 1); c->patch = 0; jst_chain++;
        }
        t->ic[icidx(pc)].pc = pc; t->ic[icidx(pc)].code = code;
        jit_enter(c, code);
        jreg_leave(t);
        return;
    }
}

static u64 jit_pc2rip(void *u) {
    u64 pc;
#ifdef PEMU_JIT_EMBED
    (void) u; pc = pemu_embed_pc(); pemu_embed_clear();
#else
    pc = (u64) ((ucontext_t*) u)->uc_mcontext.pc;
#endif
    if (!pc || !jbase || pc < (u64) jbase || pc >= (u64) jcur) return 0;
    u32 off = (u32)(pc - (u64) jbase), lo = 0, hi = __atomic_load_n(&jmapn, __ATOMIC_ACQUIRE);
    if (!hi) return 0;
    while (hi - lo > 1) { u32 mid = (lo + hi) / 2; if (jm_off[mid] <= off) lo = mid; else hi = mid; }
    return jm_off[lo] <= off ? jm_pc[lo] : 0;
}
static void jit_fault(void *u, CPU *c) {                          /* fault in translated code: guest rip and the pinned registers come from the signal context */
    u64 regs[8], r;
#ifdef PEMU_JIT_EMBED
    for (int i = 0; i < 8; i++) regs[i] = pemu_embed_x(jpin_reg[i]);
#else
    for (int i = 0; i < 8; i++) regs[i] = ((ucontext_t*) u)->uc_mcontext.regs[jpin_reg[i]];
#endif
    r = jit_pc2rip(u);                                            /* (the embedded interpreter's state is cleared here) */
    if (!r) return;
    c->rip = r;
    if (g_jpin) memcpy(c->r, regs, sizeof regs);
}
static void jit_region_abort(void) { __atomic_store_n(&t_jt.active, 0, __ATOMIC_SEQ_CST); }      /* after a fault longjmp'ed out of the cache */

static void jit_post_syscall(CPU *c, u64 n, long r) {
    u64 a0 = c->r[RDI], a1 = c->r[RSI], a2 = c->r[RDX], a3 = c->r[10];
    if (n != 9 && n != 10 && n != 11 && n != 25) return;
    pthread_mutex_lock(&jmu);                                     /* jpg / jrwx are shared with the translators */
    switch (n) {
    case 9:                                                       /* mmap */
        if ((u64) r >= 0xfffffffffffff000ULL) break;
        if ((a2 & PROT_EXEC) && (a2 & PROT_WRITE)) jit_note_rwx((u64) r, a1);
        if ((a3 & MAP_FIXED) && jit_has_code(a0, a1)) __atomic_store_n(&g_jflush_req, 1, __ATOMIC_RELEASE);
        break;
    case 10:                                                      /* mprotect */
        if (r) break;
        if ((a2 & PROT_EXEC) && (a2 & PROT_WRITE)) jit_note_rwx(a0, a1);
        if (jit_has_code(a0, a1)) __atomic_store_n(&g_jflush_req, 1, __ATOMIC_RELEASE);
        break;
    case 11: if (!r && jit_has_code(a0, a1)) __atomic_store_n(&g_jflush_req, 1, __ATOMIC_RELEASE); break;        /* munmap */
    case 25: if ((u64) r < 0xfffffffffffff000ULL && jit_has_code(a0, a1)) __atomic_store_n(&g_jflush_req, 1, __ATOMIC_RELEASE); break;   /* mremap */
    default: break;
    }
    pthread_mutex_unlock(&jmu);
}
static void jit_report(void) {
    if (!g_jstats) return;
    g_jstats = 0;
    fprintf(stderr, "pemu jit: %llu blocks, %llu guest instructions translated (%.1f AArch64 instructions each), %llu chained exits, %llu cache flushes\n",
            jst_blocks, jst_ins, jst_ins ? (double) jst_words / (double) jst_ins : 0.0, jst_chain, jst_flush);
    fprintf(stderr, "pemu jit: %u translated instructions fall back to the interpreter; opcodes by number of sites:", jst_fb);
    for (int k = 0; k < 8; k++) {
        int b = -1; for (int i = 0; i < 512; i++) if (jfbh[i] && (b < 0 || jfbh[i] > jfbh[b])) b = i;
        if (b < 0) break;
        fprintf(stderr, " %s%02x x%u", b >= 256 ? "0f " : "", b & 255, jfbh[b]); jfbh[b] = 0;
    }
    fprintf(stderr, "\n");
    fprintf(stderr, "pemu jit: %llu interpreter fallbacks executed; most executed opcodes (count):", jst_dyn);
    for (int k = 0; k < 12; k++) {
        int b = -1; for (int i = 0; i < 512; i++) if (jdyn[i] && (b < 0 || jdyn[i] > jdyn[b])) b = i;
        if (b < 0) break;
        fprintf(stderr, " %s%02x x%llu", b >= 256 ? "0f " : "", b & 255, jdyn[b]); jdyn[b] = 0;
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
    jh = jmap(sizeof(JEnt) << JH_BITS); jpg = jmap(sizeof(u64) << JPG_BITS);
    jm_off = jmap(4ul * JMAP_CAP); jm_pc = jmap(8ul * JMAP_CAP);
    if (!jh || !jpg || !jm_off || !jm_pc) return -1;
    { const char *eo = getenv("PEMU_JIT_OPT"); if (eo && *eo) g_jopt = (unsigned) strtoul(eo, NULL, 0); }
    g_jpin = !!(g_jopt & JOPT_PIN);
    jcur = jbase;
    jenter = jcur;                                                /* void jenter(CPU *c, void *code, volatile u64 *pend, JEnt *ic) */
    E(0xA9800000u | (u32)(-12 & 0x7f) << 15 | 30u << 10 | 31u << 5 | 29u);           /* stp x29, x30, [sp, #-96]! */
    a_stp(27, 28, 31, 16); a_stp(25, 26, 31, 32); a_stp(19, 20, 31, 48); a_stp(21, 22, 31, 64); a_stp(23, 24, 31, 80);
    a_mov(28, 0); a_mov(27, 2); a_mov(26, 3);
    j_pin_fill();                                                 /* guest rax..rdi into their host registers */
    E(0xD5033FDFu);                                               /* isb: code other threads translated is visible */
    a_br(1);
    jepi = jcur;
    j_pin_spill();
    a_ldp(25, 26, 31, 32); a_ldp(27, 28, 31, 16); a_ldp(19, 20, 31, 48); a_ldp(21, 22, 31, 64); a_ldp(23, 24, 31, 80);
    E(0xA8C00000u | 12u << 15 | 30u << 10 | 31u << 5 | 29u);                         /* ldp x29, x30, [sp], #96 */
    E(0xD65F03C0u);                                               /* ret */
    jcur = jbase + JTRAMP;
    j_sync_icache(jbase, jcur);
    g_jstats = getenv("PEMU_JIT_STATS") != NULL;
    return 0;
}
/* Every guest thread registers once before it runs translated code; see the comment at JT. */
static void jit_thread_init(void) {
    JT *t = &t_jt;
    t->ic = jmap(sizeof(JEnt) << JIC_BITS);
    if (!t->ic) return;                                           /* this thread stays with the interpreter (run() checks t_jt.ic) */
    t->pp = PENDP(); t->active = 0;
    pthread_mutex_lock(&jmu);
    t->gen = g_jgen; t->next = jreg; jreg = t;
    pthread_mutex_unlock(&jmu);
}
static void jit_thread_exit(void) {
    JT *t = &t_jt;
    if (!t->ic) return;
    pthread_mutex_lock(&jmu);
    for (JT **pp = &jreg; *pp; pp = &(*pp)->next) if (*pp == t) { *pp = t->next; break; }
    pthread_mutex_unlock(&jmu);
    munmap(t->ic, sizeof(JEnt) << JIC_BITS); t->ic = NULL;
}
static void jit_mt_enable(void) {                                 /* the guest is about to get its second thread */
    if (g_jmt) return;
    pthread_mutex_lock(&jmu);
    g_jmt = 1; jit_stw_locked();                                  /* everything translated so far has no memory barriers */
    pthread_mutex_unlock(&jmu);
}
static void jit_atfork(int phase) {                               /* 0 before fork(), 1 in the parent after it, 2 in the child */
    if (phase == 0) { pthread_mutex_lock(&jmu); return; }
    if (phase == 2) { jreg = t_jt.ic ? &t_jt : NULL; t_jt.next = NULL; }      /* the other threads do not exist in the child */
    pthread_mutex_unlock(&jmu);
}
#endif /* HAVE_JIT */

static void run(CPU*c) {
#ifdef HAVE_JIT
    if (g_jit_on) { jit_thread_init(); jit_rsb_clear(c); }
#endif
    if (sigsetjmp(g_jb, 1)) {
        int s = g_fault; sigset_t ss; sigemptyset(&ss); sigaddset(&ss, s); sigprocmask(SIG_UNBLOCK, &ss, NULL);
#ifdef HAVE_JIT
        c->patch = 0; jit_region_abort();
#endif
        deliver_sig(c, s);
    }
    for (;;) {
#ifdef HAVE_JIT
        if (g_jit_on && t_jt.ic) { if (*PENDP()) { c->patch = 0; check_pending(c); } jit_dispatch(c); continue; }
#endif
        if (*PENDP()) check_pending(c);
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
