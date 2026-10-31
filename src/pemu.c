/* pemu - small aarch64 user-mode emulator for x86_64 Linux hosts.
 * SPDX-License-Identifier: Apache-2.0
 *
 * A minimal, self-written replacement for qemu-user, meant to be used through
 * PRX_QEMU. It interprets AArch64 code, maps the guest 1:1 into the host
 * address space (so guest pointers are host pointers) and translates guest
 * Linux syscalls to host syscalls.
 *
 * Command line (compatible with how prx calls qemu):
 *     pemu [-0 argv0] [-L prefix] [-E VAR=val] [-U VAR] [-cpu x] [-strace] prog [args...]
 * Environment: QEMU_SET_ENV, QEMU_UNSET_ENV, QEMU_STRACE are honoured.
 *
 * Supported: integer ISA, load/store (incl. exclusives, SIMD&FP register
 * forms, LD1/ST1), scalar FP, a small NEON subset, fork/exec, signals.
 * NOT supported: threads (CLONE_THREAD), LSE atomics, most of NEON, SVE.
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
#include <setjmp.h>
#include <math.h>
#include <elf.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <sys/auxv.h>
#include <sys/random.h>

#ifndef __x86_64__
#error "pemu currently targets x86_64 hosts (an aarch64 host does not need it)"
#endif

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int64_t s64; typedef int32_t s32;
typedef u16 __attribute__((may_alias, aligned(1))) u16u;
typedef u32 __attribute__((may_alias, aligned(1))) u32u;
typedef u64 __attribute__((may_alias, aligned(1))) u64u;

typedef struct {
    u64 x[32];          /* x[31] unused: sp kept separately */
    u64 sp, pc, tpidr, fpcr, fpsr;
    u32 nzcv;           /* N=8 Z=4 C=2 V=1 */
    u64 v[32][2];
    u64 xaddr, xval; int xsz;   /* exclusive monitor */
} CPU;

static CPU cpu;
static int g_trace;
static char g_self[4096];
static u64 g_tramp;

/* ------------------------------------------------------------ helpers */
#define X(r)     ((r) == 31 ? 0 : c->x[r])
#define SET(r,v) do { u64 _sv = (v); if ((r) != 31) c->x[r] = _sv; } while (0)
#define XS(r)    ((r) == 31 ? c->sp : c->x[r])
#define SETS(r,v) do { u64 _sv = (v); if ((r) == 31) c->sp = _sv; else c->x[r] = _sv; } while (0)
#define M32 0xffffffffULL

static inline s64 sext(u64 v, int bits) { return (s64)(v << (64-bits)) >> (64-bits); }
static inline u64 msk(int sf) { return sf ? ~0ULL : M32; }

static long hs(long nr, long a, long b, long c_, long d, long e, long f) {
    long r = syscall(nr, a, b, c_, d, e, f);
    return r == -1 ? -errno : r;
}

static u64 mrd(u64 a, int n) {
    switch (n) { case 1: return *(u8*)a; case 2: return *(u16u*)a; case 4: return *(u32u*)a; default: return *(u64u*)a; }
}
static void mwr(u64 a, u64 v, int n) {
    switch (n) { case 1: *(u8*)a = (u8)v; break; case 2: *(u16u*)a = (u16)v; break; case 4: *(u32u*)a = (u32)v; break; default: *(u64u*)a = v; }
}

static u64 adc(CPU*c, int sf, u64 a, u64 b, int cin, int setf) {
    u64 m = msk(sf), r; int C, V;
    a &= m; b &= m;
    if (sf) {
        unsigned __int128 s = (unsigned __int128) a + b + cin;
        r = (u64) s; C = (int)((s >> 64) & 1); V = (int)((((a ^ r) & (b ^ r)) >> 63) & 1);
    } else {
        u64 s = a + b + cin;
        r = s & m; C = (int)((s >> 32) & 1); V = (int)((((a ^ r) & (b ^ r)) >> 31) & 1);
    }
    if (setf) c->nzcv = (u32)((((r >> (sf ? 63 : 31)) & 1) << 3) | ((r == 0) << 2) | (C << 1) | V);
    return r;
}
static u64 addsub(CPU*c, int sf, u64 a, u64 b, int sub, int setf) {
    return adc(c, sf, a, sub ? ~b : b, sub, setf);
}
static int cond(CPU*c, int cc) {
    int n = (c->nzcv >> 3) & 1, z = (c->nzcv >> 2) & 1, cf = (c->nzcv >> 1) & 1, v = c->nzcv & 1, r;
    switch (cc >> 1) {
    case 0: r = z; break; case 1: r = cf; break; case 2: r = n; break; case 3: r = v; break;
    case 4: r = cf && !z; break; case 5: r = n == v; break; case 6: r = n == v && !z; break;
    default: r = 1; }
    if ((cc & 1) && cc != 15) r = !r;
    return r;
}
static u64 shiftv(u64 v, int type, int amt, int sf) {
    int n = sf ? 64 : 32;
    u64 m = msk(sf);
    v &= m; amt &= n - 1;
    switch (type) {
    case 0: return (v << amt) & m;
    case 1: return v >> amt;
    case 2: return (u64)((sf ? (s64) v : (s64)(s32) v) >> amt) & m;
    default: return amt ? ((v >> amt) | (v << (n - amt))) & m : v;
    }
}
static u64 extendv(u64 v, int opt, int sh) {
    switch (opt) {
    case 0: v &= 0xff; break; case 1: v &= 0xffff; break; case 2: v &= M32; break;
    case 4: v = (u64) sext(v, 8); break; case 5: v = (u64) sext(v, 16); break; case 6: v = (u64) sext(v, 32); break;
    }
    return v << sh;
}
static u64 ror_n(u64 w, int r, int n) {
    u64 m = n == 64 ? ~0ULL : ((1ULL << n) - 1);
    r %= n;
    return r ? ((w >> r) | (w << (n - r))) & m : w & m;
}
static int decode_bm(int N, int imms, int immr, int sf, u64*wm, u64*tm) {
    unsigned x = ((unsigned) N << 6) | (~imms & 0x3f);
    if (!x) return -1;
    int len = 31 - __builtin_clz(x);
    if (len < 1 || (!sf && N)) return -1;
    int es = 1 << len, lv = es - 1;
    int S = imms & lv, R = immr & lv, d = (S - R) & lv;
    u64 w = S + 1 == 64 ? ~0ULL : ((1ULL << (S + 1)) - 1);
    u64 t = d + 1 == 64 ? ~0ULL : ((1ULL << (d + 1)) - 1);
    w = ror_n(w, R, es);
    int ds = sf ? 64 : 32;
    u64 W = 0, T = 0;
    for (int i = 0; i < ds; i += es) { W |= w << i; T |= t << i; }
    *wm = W; *tm = T;
    return 0;
}

/* ------------------------------------------------------------ signals */
struct gsa { u64 handler, flags, mask; };
static struct gsa gsa[65];
static volatile u64 pend;
static siginfo_t pinfo[65];
static sigjmp_buf g_jb;
static volatile int g_fault; static volatile u64 g_faddr;

static void hsig(int s, siginfo_t*si, void*u) { (void) u; if (si) pinfo[s] = *si; __atomic_or_fetch(&pend, 1ULL << s, __ATOMIC_SEQ_CST); }
static void hsegv(int s, siginfo_t*si, void*u) {
    (void) u;
    if (gsa[s].handler > 1) { g_fault = s; g_faddr = (u64) si->si_addr; siglongjmp(g_jb, 1); }
    fprintf(stderr, "pemu: guest fault (signal %d) at addr=%p pc=%#llx\n", s, si->si_addr, (unsigned long long) cpu.pc);
    signal(s, SIG_DFL); raise(s);
    _exit(128 + s);
}
#define FRAME 1136
static void deliver_sig(CPU*c, int s) {
    struct gsa*h = &gsa[s];
    u64 sp = (c->sp - FRAME) & ~15ULL;
    memset((void*) sp, 0, FRAME);
    memcpy((void*) sp, &pinfo[s], 128);
    if (g_fault == s) { ((int*)sp)[0] = s; }
    u64 uc = sp + 128;
    u64 old = 0;
    syscall(SYS_rt_sigprocmask, SIG_BLOCK, NULL, &old, 8);
    *(u64*)(uc + 40) = old;
    u64 mc = uc + 176;
    *(u64*)(mc + 0) = g_fault == s ? g_faddr : 0;
    memcpy((void*)(mc + 8), c->x, 31 * 8);
    *(u64*)(mc + 256) = c->sp; *(u64*)(mc + 264) = c->pc; *(u64*)(mc + 272) = (u64) c->nzcv << 28;
    memcpy((void*)(mc + 288), c->v, 512);
    u64 nm = old | h->mask | ((h->flags & 0x40000000) ? 0 : (1ULL << (s - 1)));
    syscall(SYS_rt_sigprocmask, SIG_SETMASK, &nm, NULL, 8);
    c->x[0] = (u64) s; c->x[1] = sp; c->x[2] = uc;
    c->sp = sp; c->pc = h->handler; c->x[30] = g_tramp;
    if (h->flags & 0x80000000u) h->handler = 0;
    g_fault = 0;
}
static void g_sigreturn(CPU*c) {
    u64 sp = c->sp, uc = sp + 128, mc = uc + 176;
    memcpy(c->x, (void*)(mc + 8), 31 * 8);
    c->sp = *(u64*)(mc + 256); c->pc = *(u64*)(mc + 264); c->nzcv = (u32)(*(u64*)(mc + 272) >> 28) & 15;
    memcpy(c->v, (void*)(mc + 288), 512);
    u64 m = *(u64*)(uc + 40);
    syscall(SYS_rt_sigprocmask, SIG_SETMASK, &m, NULL, 8);
}
static void check_pending(CPU*c) {
    while (pend) {
        u64 p = pend; int s = __builtin_ctzll(p) ;
        __atomic_and_fetch(&pend, ~(1ULL << s), __ATOMIC_SEQ_CST);
        if (gsa[s].handler > 1) deliver_sig(c, s);
    }
}

/* ------------------------------------------------------------ syscalls */
static short tbl[512];
static u64 brk_base, brk_cur, brk_end;

static void init_tbl(void) {
    memset(tbl, -1, sizeof tbl);
#define T(a,b) tbl[a] = (b)
    T(5,SYS_setxattr); T(6,SYS_lsetxattr); T(7,SYS_fsetxattr); T(8,SYS_getxattr); T(9,SYS_lgetxattr);
    T(10,SYS_fgetxattr); T(11,SYS_listxattr); T(12,SYS_llistxattr); T(13,SYS_flistxattr);
    T(14,SYS_removexattr); T(15,SYS_lremovexattr); T(16,SYS_fremovexattr);
    T(17,SYS_getcwd); T(19,SYS_eventfd2); T(20,SYS_epoll_create1); T(23,SYS_dup); T(24,SYS_dup3);
    T(26,SYS_inotify_init1); T(27,SYS_inotify_add_watch); T(28,SYS_inotify_rm_watch); T(29,SYS_ioctl);
    T(32,SYS_flock); T(33,SYS_mknodat); T(34,SYS_mkdirat); T(35,SYS_unlinkat); T(36,SYS_symlinkat);
    T(37,SYS_linkat); T(38,SYS_renameat); T(40,SYS_mount); T(39,SYS_umount2);
    T(43,SYS_statfs); T(44,SYS_fstatfs); T(45,SYS_truncate); T(46,SYS_ftruncate); T(47,SYS_fallocate);
    T(48,SYS_faccessat); T(49,SYS_chdir); T(50,SYS_fchdir); T(51,SYS_chroot); T(52,SYS_fchmod);
    T(53,SYS_fchmodat); T(54,SYS_fchownat); T(55,SYS_fchown); T(57,SYS_close); T(59,SYS_pipe2);
    T(61,SYS_getdents64); T(62,SYS_lseek); T(63,SYS_read); T(64,SYS_write); T(65,SYS_readv);
    T(66,SYS_writev); T(67,SYS_pread64); T(68,SYS_pwrite64); T(69,SYS_preadv); T(70,SYS_pwritev);
    T(71,SYS_sendfile); T(72,SYS_pselect6); T(73,SYS_ppoll); T(76,SYS_splice); T(78,SYS_readlinkat);
    T(81,SYS_sync); T(82,SYS_fsync); T(83,SYS_fdatasync); T(84,SYS_sync_file_range);
    T(85,SYS_timerfd_create); T(86,SYS_timerfd_settime); T(87,SYS_timerfd_gettime); T(88,SYS_utimensat);
    T(90,SYS_capget); T(91,SYS_capset); T(92,SYS_personality); T(95,SYS_waitid); T(96,SYS_set_tid_address);
    T(98,SYS_futex); T(99,SYS_set_robust_list); T(100,SYS_get_robust_list); T(101,SYS_nanosleep);
    T(102,SYS_getitimer); T(103,SYS_setitimer); T(112,SYS_clock_settime); T(113,SYS_clock_gettime);
    T(114,SYS_clock_getres); T(115,SYS_clock_nanosleep); T(116,SYS_syslog);
    T(118,SYS_sched_setparam); T(119,SYS_sched_setscheduler); T(120,SYS_sched_getscheduler);
    T(121,SYS_sched_getparam); T(122,SYS_sched_setaffinity); T(123,SYS_sched_getaffinity);
    T(124,SYS_sched_yield); T(125,SYS_sched_get_priority_max); T(126,SYS_sched_get_priority_min);
    T(129,SYS_kill); T(130,SYS_tkill); T(131,SYS_tgkill); T(135,SYS_rt_sigprocmask); T(136,SYS_rt_sigpending);
    T(137,SYS_rt_sigtimedwait); T(138,SYS_rt_sigqueueinfo); T(133,SYS_rt_sigsuspend);
    T(140,SYS_setpriority); T(141,SYS_getpriority); T(143,SYS_setregid); T(144,SYS_setgid);
    T(145,SYS_setreuid); T(146,SYS_setuid); T(147,SYS_setresuid); T(148,SYS_getresuid);
    T(149,SYS_setresgid); T(150,SYS_getresgid); T(151,SYS_setfsuid); T(152,SYS_setfsgid);
    T(153,SYS_times); T(154,SYS_setpgid); T(155,SYS_getpgid); T(156,SYS_getsid); T(157,SYS_setsid);
    T(158,SYS_getgroups); T(159,SYS_setgroups); T(161,SYS_sethostname); T(163,SYS_getrlimit);
    T(164,SYS_setrlimit); T(165,SYS_getrusage); T(166,SYS_umask); T(167,SYS_prctl); T(168,SYS_getcpu);
    T(169,SYS_gettimeofday); T(172,SYS_getpid); T(173,SYS_getppid); T(174,SYS_getuid);
    T(175,SYS_geteuid); T(176,SYS_getgid); T(177,SYS_getegid); T(178,SYS_gettid); T(179,SYS_sysinfo);
    T(198,SYS_socket); T(199,SYS_socketpair); T(200,SYS_bind); T(201,SYS_listen); T(202,SYS_accept);
    T(203,SYS_connect); T(204,SYS_getsockname); T(205,SYS_getpeername); T(206,SYS_sendto);
    T(207,SYS_recvfrom); T(208,SYS_setsockopt); T(209,SYS_getsockopt); T(210,SYS_shutdown);
    T(211,SYS_sendmsg); T(212,SYS_recvmsg); T(213,SYS_readahead); T(215,SYS_munmap); T(216,SYS_mremap);
    T(222,SYS_mmap); T(223,SYS_fadvise64); T(226,SYS_mprotect); T(227,SYS_msync); T(228,SYS_mlock);
    T(229,SYS_munlock); T(230,SYS_mlockall); T(231,SYS_munlockall); T(232,SYS_mincore); T(233,SYS_madvise);
    T(242,SYS_accept4); T(243,SYS_recvmmsg); T(260,SYS_wait4); T(261,SYS_prlimit64); T(267,SYS_syncfs);
    T(269,SYS_sendmmsg); T(276,SYS_renameat2); T(278,SYS_getrandom); T(279,SYS_memfd_create);
    T(285,SYS_copy_file_range); T(291,SYS_statx); T(439,SYS_faccessat2);
#undef T
}

/* open(2) flag differences between asm-generic (aarch64) and x86_64 */
static long fl_g2h(long f) {
    long r = f & ~(040000L | 0100000L | 0200000L | 0400000L);
    if (f & 040000)  r |= O_DIRECTORY;
    if (f & 0100000) r |= O_NOFOLLOW;
    if (f & 0200000) r |= O_DIRECT;
    return r;                    /* O_LARGEFILE dropped (always 64-bit) */
}
static long fl_h2g(long f) {
    long r = f & ~(O_DIRECTORY | O_NOFOLLOW | O_DIRECT | 0100000L);
    if (f & O_DIRECTORY) r |= 040000;
    if (f & O_NOFOLLOW)  r |= 0100000;
    if (f & O_DIRECT)    r |= 0200000;
    return r;
}
static void cv_stat(const struct stat*s, u8*g) {
    memset(g, 0, 128);
    *(u64*)(g + 0) = s->st_dev; *(u64*)(g + 8) = s->st_ino;
    *(u32*)(g + 16) = s->st_mode; *(u32*)(g + 20) = (u32) s->st_nlink;
    *(u32*)(g + 24) = s->st_uid; *(u32*)(g + 28) = s->st_gid;
    *(u64*)(g + 32) = s->st_rdev; *(s64*)(g + 48) = s->st_size;
    *(s32*)(g + 56) = (s32) s->st_blksize; *(s64*)(g + 64) = s->st_blocks;
    *(s64*)(g + 72) = s->st_atim.tv_sec; *(u64*)(g + 80) = (u64) s->st_atim.tv_nsec;
    *(s64*)(g + 88) = s->st_mtim.tv_sec; *(u64*)(g + 96) = (u64) s->st_mtim.tv_nsec;
    *(s64*)(g + 104) = s->st_ctim.tv_sec; *(u64*)(g + 112) = (u64) s->st_ctim.tv_nsec;
}
static int is_aarch64_elf(const char*p) {
    int fd = open(p, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    unsigned char h[20];
    ssize_t n = read(fd, h, 20);
    close(fd);
    return n == 20 && !memcmp(h, "\177ELF", 4) && h[4] == 2 && *(u16*)(h + 18) == EM_AARCH64;
}
static long do_execve(const char*path, char**argv, char**envp) {
    if (!is_aarch64_elf(path)) return hs(SYS_execve, (long) path, (long) argv, (long) envp, 0, 0, 0);
    int n = 0; while (argv && argv[n]) n++;
    char**nv = calloc((size_t) n + 6, sizeof *nv);
    int k = 0;
    nv[k++] = g_self; nv[k++] = "-0"; nv[k++] = n ? argv[0] : (char*) path; nv[k++] = (char*) path;
    for (int i = 1; i < n; i++) nv[k++] = argv[i];
    long r = hs(SYS_execve, (long) g_self, (long) nv, (long) envp, 0, 0, 0);
    free(nv);
    return r;
}

static long gsys(CPU*c, int*noset) {
    u64 n = c->x[8], a0 = c->x[0], a1 = c->x[1], a2 = c->x[2], a3 = c->x[3], a4 = c->x[4], a5 = c->x[5];
    long r;
    (void) noset;
    switch (n) {
    case 93: case 94: return hs(SYS_exit_group, (long) a0, 0, 0, 0, 0, 0);
    case 56: return hs(SYS_openat, (long) a0, (long) a1, fl_g2h((long) a2), (long) a3, 0, 0);
    case 59: return hs(SYS_pipe2, (long) a0, (long) a1, 0, 0, 0, 0);
    case 25:
        if (a1 == F_GETFL) { r = hs(SYS_fcntl, (long) a0, F_GETFL, 0, 0, 0, 0); return r < 0 ? r : fl_h2g(r); }
        if (a1 == F_SETFL) return hs(SYS_fcntl, (long) a0, F_SETFL, fl_g2h((long) a2), 0, 0, 0);
        return hs(SYS_fcntl, (long) a0, (long) a1, (long) a2, 0, 0, 0);
    case 79: { struct stat st; r = hs(SYS_newfstatat, (long) a0, (long) a1, (long) &st, (long) a3, 0, 0); if (r == 0) cv_stat(&st, (u8*) a2); return r; }
    case 80: { struct stat st; r = hs(SYS_fstat, (long) a0, (long) &st, 0, 0, 0, 0); if (r == 0) cv_stat(&st, (u8*) a1); return r; }
    case 160: { struct utsname u; r = hs(SYS_uname, (long) &u, 0, 0, 0, 0, 0); if (r == 0) { strcpy(u.machine, "aarch64"); memcpy((void*) a0, &u, sizeof u); } return r; }
    case 214: /* brk */
        if (a0 >= brk_base && a0 <= brk_end) brk_cur = a0;
        return (long) brk_cur;
    case 132: return 0;                              /* sigaltstack: accepted, ignored */
    case 134: {                                      /* rt_sigaction */
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
            if (sigaction(s, &sa, NULL) < 0) return -errno;
            gsa[s] = nw;
        }
        if (a2) *(struct gsa*) a2 = old;
        return 0;
    }
    case 139: g_sigreturn(c); *noset = 1; return 0;
    case 21: {                                       /* epoll_ctl: packed host struct */
        struct epoll_event ev; struct epoll_event*p = NULL;
        if (a3) { ev.events = *(u32*) a3; ev.data.u64 = *(u64*)(a3 + 8); p = &ev; }
        return hs(SYS_epoll_ctl, (long) a0, (long) a1, (long) a2, (long) p, 0, 0);
    }
    case 22: {                                       /* epoll_pwait */
        int max = (int) a2; if (max <= 0 || max > 4096) return -EINVAL;
        struct epoll_event*ev = malloc(sizeof *ev * (size_t) max);
        r = hs(SYS_epoll_pwait, (long) a0, (long) ev, max, (long) a3, (long) a4, (long) a5);
        for (long i = 0; i < r; i++) { u8*g = (u8*) a1 + 16 * i; *(u64*) g = 0; *(u32*) g = ev[i].events; *(u64*)(g + 8) = ev[i].data.u64; }
        free(ev); return r;
    }
    case 220: {                                      /* clone: fork-style only */
        u64 flags = a0;
        if (flags & 0x10000 /*CLONE_THREAD*/) return -ENOSYS;
        long pid = hs(SYS_fork, 0, 0, 0, 0, 0, 0);
        if (pid == 0) {
            if (a1) c->sp = a1;
            if (flags & 0x01000000) *(u32*) a4 = (u32) getpid();     /* CHILD_SETTID */
            if (flags & 0x00080000 /*SETTLS*/) c->tpidr = a3;
            c->x[0] = 0; *noset = 1;
        } else if (pid > 0 && (flags & 0x00100000)) *(u32*) a2 = (u32) pid;   /* PARENT_SETTID */
        return pid;
    }
    case 221: return do_execve((const char*) a0, (char**) a1, (char**) a2);
    case 281: case 435: case 293: case 277: return -ENOSYS;
    default:
        if (n < 512 && tbl[n] >= 0) return hs(tbl[n], (long) a0, (long) a1, (long) a2, (long) a3, (long) a4, (long) a5);
        if (g_trace) fprintf(stderr, "pemu: unsupported syscall %llu\n", (unsigned long long) n);
        return -ENOSYS;
    }
}
static void do_svc(CPU*c) {
    int noset = 0;
    u64 n = c->x[8];
    long r = gsys(c, &noset);
    if (g_trace) fprintf(stderr, "pemu: syscall %llu -> %ld\n", (unsigned long long) n, r);
    if (!noset) c->x[0] = (u64) r;
}

/* ------------------------------------------------------------ FP / SIMD */
static double fget(CPU*c, int r, int type) {
    if (type == 0) { float f; memcpy(&f, &c->v[r][0], 4); return f; }
    double d; memcpy(&d, &c->v[r][0], 8); return d;
}
static void fput(CPU*c, int r, int type, double v) {
    c->v[r][0] = 0; c->v[r][1] = 0;
    if (type == 0) { float f = (float) v; memcpy(&c->v[r][0], &f, 4); } else memcpy(&c->v[r][0], &v, 8);
}
static u64 fcvt_int(double v, int sf, int uns, int mode) {
    if (isnan(v)) return 0;
    switch (mode) { case 0: v = nearbyint(v); break; case 1: v = ceil(v); break; case 2: v = floor(v); break; case 3: v = trunc(v); break; default: v = round(v); }
    if (uns) {
        double mx = sf ? 18446744073709551616.0 : 4294967296.0;
        if (v <= 0) return 0;
        if (v >= mx) return msk(sf);
        return (u64) v;
    }
    double hi = sf ? 9223372036854775808.0 : 2147483648.0;
    if (v >= hi) return sf ? 0x7fffffffffffffffULL : 0x7fffffffULL;
    if (v < -hi) return sf ? 0x8000000000000000ULL : 0x80000000ULL;
    return (u64)(s64) v & msk(sf);
}
static int fp_insn(CPU*c, u32 i) {
    int type = (i >> 22) & 3, rn = (i >> 5) & 31, rd = i & 31, rm = (i >> 16) & 31;
    int sf = i >> 31;
    if ((i & 0x7F20FC00) == 0x1E200000) {                      /* FP <-> integer */
        int rmode = (i >> 19) & 3, opc = (i >> 16) & 7;
        if (opc == 6 || opc == 7) {                            /* FMOV general */
            if (sf && type == 1 && rmode == 0) { if (opc == 6) SET(rd, c->v[rn][0]); else { c->v[rd][0] = X(rn); c->v[rd][1] = 0; } return 0; }
            if (!sf && type == 0 && rmode == 0) { if (opc == 6) SET(rd, c->v[rn][0] & M32); else { c->v[rd][0] = X(rn) & M32; c->v[rd][1] = 0; } return 0; }
            if (sf && type == 2 && rmode == 1) { if (opc == 6) SET(rd, c->v[rn][1]); else c->v[rd][1] = X(rn); return 0; }
            return -1;
        }
        if (type > 1) return -1;
        if (opc == 2 || opc == 3) {                            /* SCVTF / UCVTF */
            double v = opc == 2 ? (double)(sf ? (s64) X(rn) : (s64)(s32) X(rn)) : (double)(sf ? X(rn) : X(rn) & M32);
            if (type == 0) { float f = opc == 2 ? (float)(sf ? (s64) X(rn) : (s64)(s32) X(rn)) : (float)(sf ? X(rn) : X(rn) & M32); c->v[rd][0] = 0; c->v[rd][1] = 0; memcpy(&c->v[rd][0], &f, 4); }
            else fput(c, rd, 1, v);
            return 0;
        }
        if (opc <= 1 || opc == 4 || opc == 5) {                /* FCVT[NPMZA][SU] */
            int mode = opc >= 4 ? 4 : rmode, uns = opc & 1;
            SET(rd, fcvt_int(fget(c, rn, type), sf, uns, mode));
            return 0;
        }
        return -1;
    }
    if (type > 1) return -1;
    if ((i & 0xFF207C00) == 0x1E204000) {                      /* 1-source */
        int op = (i >> 15) & 0x3f;
        double v = fget(c, rn, type);
        switch (op) {
        case 0: fput(c, rd, type, v); if (type == 1) { u64 t = c->v[rn][0]; c->v[rd][0] = t; } else { u64 t = c->v[rn][0] & M32; c->v[rd][0] = t; } return 0;
        case 1: fput(c, rd, type, fabs(v)); return 0;
        case 2: { u64 t = c->v[rn][0]; if (type) t ^= 1ULL << 63; else t = (t ^ (1ULL << 31)) & M32; c->v[rd][0] = t; c->v[rd][1] = 0; return 0; }
        case 3: fput(c, rd, type, sqrt(v)); return 0;
        case 4: fput(c, rd, 0, v); return 0;
        case 5: fput(c, rd, 1, v); return 0;
        case 8: fput(c, rd, type, nearbyint(v)); return 0;
        case 9: fput(c, rd, type, ceil(v)); return 0;
        case 10: fput(c, rd, type, floor(v)); return 0;
        case 11: fput(c, rd, type, trunc(v)); return 0;
        case 12: fput(c, rd, type, round(v)); return 0;
        case 14: case 15: fput(c, rd, type, nearbyint(v)); return 0;
        }
        return -1;
    }
    if ((i & 0xFF20FC07) == 0x1E202000) {                      /* FCMP / FCMPE */
        double a = fget(c, rn, type), b = (i & 8) ? 0.0 : fget(c, rm, type);
        c->nzcv = (isnan(a) || isnan(b)) ? 3 : a == b ? 6 : a < b ? 8 : 2;
        return 0;
    }
    if ((i & 0xFF201FE0) == 0x1E201000) {                      /* FMOV imm */
        u32 imm = (i >> 13) & 0xff, b6 = (imm >> 6) & 1, sg = imm >> 7;
        c->v[rd][1] = 0;
        if (type == 0) c->v[rd][0] = ((u64) sg << 31) | ((u64)(!b6) << 30) | ((u64)(b6 ? 0x1f : 0) << 25) | ((u64)(imm & 0x3f) << 19);
        else c->v[rd][0] = ((u64) sg << 63) | ((u64)(!b6) << 62) | ((u64)(b6 ? 0xff : 0) << 54) | ((u64)(imm & 0x3f) << 48);
        return 0;
    }
    if ((i & 0xFF200C00) == 0x1E200800) {                      /* 2-source */
        double a = fget(c, rn, type), b = fget(c, rm, type), r;
        switch ((i >> 12) & 15) {
        case 0: r = a * b; break; case 1: r = a / b; break; case 2: r = a + b; break; case 3: r = a - b; break;
        case 4: case 6: r = isnan(a) || isnan(b) ? NAN : fmax(a, b); break;
        case 5: case 7: r = isnan(a) || isnan(b) ? NAN : fmin(a, b); break;
        case 8: r = -(a * b); break;
        default: return -1; }
        fput(c, rd, type, r);
        return 0;
    }
    if ((i & 0xFF200C00) == 0x1E200C00) {                      /* FCSEL */
        int src = cond(c, (i >> 12) & 15) ? rn : rm;
        u64 t = c->v[src][0]; if (type == 0) t &= M32;
        c->v[rd][0] = t; c->v[rd][1] = 0;
        return 0;
    }
    if ((i & 0xFF000000) == 0x1F000000) {                      /* FMADD family */
        int ra = (i >> 10) & 31, o1 = (i >> 21) & 1, o0 = (i >> 15) & 1;
        double a = fget(c, rn, type), b = fget(c, rm, type), d = fget(c, ra, type);
        if (o1) a = -a;
        if (o0) d = -d;
        double r = type == 0 ? (double) fmaf((float) a, (float) b, (float) d) : fma(a, b, d);
        fput(c, rd, type, r);
        return 0;
    }
    return -1;
}
static int simd_insn(CPU*c, u32 i) {
    int rd = i & 31, rn = (i >> 5) & 31, rm = (i >> 16) & 31, q = (i >> 30) & 1;
    if ((i & 0x9F200400) == 0x0E200400 && ((i >> 11) & 31) == 3) {  /* 3-same logical */
        int u = (i >> 29) & 1, sz = (i >> 22) & 3;
        for (int k = 0; k < (q ? 2 : 1); k++) {
            u64 a = c->v[rn][k], b = c->v[rm][k], d = c->v[rd][k], r;
            if (!u) r = sz == 0 ? a & b : sz == 1 ? a & ~b : sz == 2 ? a | b : a | ~b;
            else r = sz == 0 ? a ^ b : sz == 1 ? (d & a) | (~d & b) : sz == 2 ? (b & a) | (~b & d) : (~b & a) | (b & d);
            c->v[rd][k] = r;
        }
        if (!q) c->v[rd][1] = 0;
        return 0;
    }
    if ((i & 0x9FF80400) == 0x0F000400) {                      /* MOVI (cmode 1110) */
        int cm = (i >> 12) & 15, op = (i >> 29) & 1;
        u64 imm8 = (((i >> 16) & 7) << 5) | ((i >> 5) & 31), v = 0;
        if (cm != 14) return -1;
        if (!op) for (int b = 0; b < 8; b++) v |= imm8 << (8 * b);
        else for (int b = 0; b < 8; b++) if (imm8 >> b & 1) v |= 0xffULL << (8 * b);
        c->v[rd][0] = v; c->v[rd][1] = q ? v : 0;
        return 0;
    }
    return -1;
}

/* ------------------------------------------------------------ load / store */
static int ldst(CPU*c, u32 i) {
    int rn = (i >> 5) & 31, rt = i & 31;
    if ((i & 0x3B000000) == 0x18000000) {                      /* literal */
        int opc = i >> 30, V = (i >> 26) & 1;
        u64 a = c->pc + (u64)(sext((i >> 5) & 0x7ffff, 19) << 2);
        if (!V) {
            if (opc == 3) return 0;
            SET(rt, opc == 0 ? mrd(a, 4) : opc == 1 ? mrd(a, 8) : (u64) sext(mrd(a, 4), 32));
        } else {
            int nb = 4 << opc; if (opc > 2) return -1;
            c->v[rt][0] = c->v[rt][1] = 0; memcpy(&c->v[rt][0], (void*) a, (size_t) nb);
        }
        return 0;
    }
    if ((i & 0x3A000000) == 0x28000000) {                      /* pair */
        int opc = i >> 30, V = (i >> 26) & 1, L = (i >> 22) & 1, idx = (i >> 23) & 3, rt2 = (i >> 10) & 31;
        int sc, nb;
        if (!V) { if (opc == 3) return -1; sc = opc == 2 ? 3 : 2; } else { if (opc == 3) return -1; sc = 2 + opc; }
        nb = 1 << sc;
        s64 off = sext((i >> 15) & 0x7f, 7) << sc;
        u64 base = XS(rn), a = base, nbase = base;
        if (idx == 1) nbase = base + (u64) off; else if (idx == 3) { a = base + (u64) off; nbase = a; } else a = base + (u64) off;
        if (!V) {
            if (L) {
                u64 v1 = mrd(a, nb), v2 = mrd(a + (u64) nb, nb);
                if (opc == 1) { v1 = (u64) sext(v1, 32); v2 = (u64) sext(v2, 32); }
                SET(rt, v1); SET(rt2, v2);
            } else { u64 v1 = X(rt), v2 = X(rt2); mwr(a, v1, nb); mwr(a + (u64) nb, v2, nb); }
        } else {
            if (L) {
                u64 t1[2] = {0, 0}, t2[2] = {0, 0};
                memcpy(t1, (void*) a, (size_t) nb); memcpy(t2, (void*)(a + (u64) nb), (size_t) nb);
                memcpy(c->v[rt], t1, 16); memcpy(c->v[rt2], t2, 16);
            } else { u64 t1[2], t2[2]; memcpy(t1, c->v[rt], 16); memcpy(t2, c->v[rt2], 16); memcpy((void*) a, t1, (size_t) nb); memcpy((void*)(a + (u64) nb), t2, (size_t) nb); }
        }
        if (idx == 1 || idx == 3) SETS(rn, nbase);
        return 0;
    }
    if ((i & 0x3B000000) == 0x38000000 || (i & 0x3B000000) == 0x39000000) {   /* register */
        int size = i >> 30, V = (i >> 26) & 1, opc = (i >> 22) & 3, sc, nb, isld, sgn = 0, to64 = 0;
        if (!V) {
            sc = size; nb = 1 << size;
            if (opc == 0) isld = 0; else if (opc == 1) isld = 1;
            else { if (size == 3) { if (opc == 2) return 0; return -1; } if (opc == 3 && size == 2) return -1; isld = 1; sgn = 1; to64 = opc == 2; }
        } else {
            if (opc >= 2 && size == 0) { nb = 16; sc = 4; isld = opc & 1; }
            else if (opc < 2) { nb = 1 << size; sc = size; isld = opc & 1; }
            else return -1;
        }
        u64 base = XS(rn), a, nbase = 0; int wb = 0;
        if ((i >> 24) & 1) a = base + ((u64)((i >> 10) & 0xfff) << sc);
        else if (!((i >> 21) & 1)) {
            s64 imm = sext((i >> 12) & 0x1ff, 9); int m = (i >> 10) & 3;
            if (m == 1) { a = base; nbase = base + (u64) imm; wb = 1; }
            else if (m == 3) { a = base + (u64) imm; nbase = a; wb = 1; }
            else a = base + (u64) imm;
        } else if (((i >> 10) & 3) == 2) {
            u64 off = extendv(X((i >> 16) & 31), (i >> 13) & 7, ((i >> 12) & 1) ? sc : 0);
            a = base + off;
        } else return -1;
        if (!V) {
            if (isld) {
                u64 v = mrd(a, nb);
                if (sgn) { v = (u64) sext(v, nb * 8); if (!to64) v &= M32; }
                SET(rt, v);
            } else mwr(a, X(rt), nb);
        } else {
            if (isld) { u64 t[2] = {0, 0}; memcpy(t, (void*) a, (size_t) nb); memcpy(c->v[rt], t, 16); }
            else { u64 t[2]; memcpy(t, c->v[rt], 16); memcpy((void*) a, t, (size_t) nb); }
        }
        if (wb) SETS(rn, nbase);
        return 0;
    }
    if ((i & 0x3F000000) == 0x08000000) {                      /* exclusive / acquire-release */
        int size = i >> 30, o2 = (i >> 23) & 1, L = (i >> 22) & 1, o1 = (i >> 21) & 1, rs = (i >> 16) & 31, nb = 1 << size;
        if (o1) return -1;
        u64 a = XS(rn);
        if (o2) {                                              /* LDAR / STLR (and LORegion variants) */
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            if (L) SET(rt, mrd(a, nb)); else mwr(a, X(rt), nb);
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            return 0;
        }
        if (L) { c->xaddr = a; c->xsz = nb; c->xval = mrd(a, nb); SET(rt, c->xval); return 0; }
        int ok = 0;
        if (c->xaddr == a && c->xsz == nb) {
            u64 nv = X(rt);
            switch (nb) {
            case 1: { u8 e = (u8) c->xval; ok = __atomic_compare_exchange_n((u8*) a, &e, (u8) nv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); break; }
            case 2: { u16 e = (u16) c->xval; ok = __atomic_compare_exchange_n((u16*) a, &e, (u16) nv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); break; }
            case 4: { u32 e = (u32) c->xval; ok = __atomic_compare_exchange_n((u32*) a, &e, (u32) nv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); break; }
            default: { u64 e = c->xval; ok = __atomic_compare_exchange_n((u64*) a, &e, nv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }
            }
        }
        c->xaddr = 0;
        SET(rs, ok ? 0 : 1);
        return 0;
    }
    if ((i & 0xBFA00000) == 0x0C000000 || (i & 0xBFA00000) == 0x0C800000) {   /* LD1/ST1 multiple structures */
        int q = (i >> 30) & 1, L = (i >> 22) & 1, op = (i >> 12) & 15, post = (i >> 23) & 1, nr;
        switch (op) { case 7: nr = 1; break; case 10: nr = 2; break; case 6: nr = 3; break; case 2: nr = 4; break; default: return -1; }
        int nb = q ? 16 : 8;
        u64 a = XS(rn);
        for (int k = 0; k < nr; k++) {
            int r = (rt + k) & 31;
            if (L) { c->v[r][0] = c->v[r][1] = 0; memcpy(c->v[r], (void*)(a + (u64)(k * nb)), (size_t) nb); }
            else memcpy((void*)(a + (u64)(k * nb)), c->v[r], (size_t) nb);
        }
        if (post) { int rm = (i >> 16) & 31; SETS(rn, a + (rm == 31 ? (u64)(nr * nb) : X(rm))); }
        return 0;
    }
    return -1;
}

/* ------------------------------------------------------------ main step */
static void undef(CPU*c, u32 i) {
    fprintf(stderr, "pemu: unsupported instruction %08x at pc=%#llx\n", i, (unsigned long long) c->pc);
    signal(SIGILL, SIG_DFL); raise(SIGILL); _exit(132);
}
static u64 rev_n(u64 v, int bytes) { u64 r = 0; for (int k = 0; k < bytes; k++) { r = (r << 8) | (v & 0xff); v >>= 8; } return r; }

static void step(CPU*c) {
    u64 pc = c->pc, npc = pc + 4;
    u32 i = *(u32*) pc;
    int sf = i >> 31, rd = i & 31, rn = (i >> 5) & 31, rm = (i >> 16) & 31;
    switch ((i >> 25) & 15) {
    case 8: case 9: {                                          /* data processing: immediate */
        if ((i & 0x1F000000) == 0x10000000) {                  /* ADR / ADRP */
            s64 imm = sext((((i >> 5) & 0x7ffff) << 2) | ((i >> 29) & 3), 21);
            SET(rd, (i >> 31) ? ((pc & ~0xfffULL) + (u64)(imm << 12)) : pc + (u64) imm);
        } else if ((i & 0x1F000000) == 0x11000000) {           /* ADD/SUB imm */
            u64 imm = (i >> 10) & 0xfff; if ((i >> 22) & 1) imm <<= 12;
            int sub = (i >> 30) & 1, S = (i >> 29) & 1;
            u64 r = addsub(c, sf, XS(rn), imm, sub, S);
            if (S) SET(rd, r); else SETS(rd, r);
        } else if ((i & 0x1F800000) == 0x12000000) {           /* logical imm */
            u64 wm, tm;
            if (decode_bm((i >> 22) & 1, (i >> 10) & 0x3f, (i >> 16) & 0x3f, sf, &wm, &tm) < 0) undef(c, i);
            int opc = (i >> 29) & 3; u64 a = X(rn), r;
            r = opc == 1 ? a | wm : opc == 2 ? a ^ wm : a & wm;
            r &= msk(sf);
            if (opc == 3) { c->nzcv = (u32)(((r >> (sf ? 63 : 31)) & 1) << 3 | (r == 0) << 2); SET(rd, r); } else SETS(rd, r);
        } else if ((i & 0x1F800000) == 0x12800000) {           /* move wide */
            int opc = (i >> 29) & 3, hw = (i >> 21) & 3; u64 imm = (u64)((i >> 5) & 0xffff) << (hw * 16);
            if (!sf && hw > 1) undef(c, i);
            if (opc == 0) SET(rd, ~imm & msk(sf));
            else if (opc == 2) SET(rd, imm);
            else if (opc == 3) SET(rd, (X(rd) & ~(0xffffULL << (hw * 16))) | imm);
            else undef(c, i);
        } else if ((i & 0x1F800000) == 0x13000000) {           /* bitfield */
            u64 wm, tm; int opc = (i >> 29) & 3, immr = (i >> 16) & 0x3f, imms = (i >> 10) & 0x3f;
            if (opc == 3 || decode_bm((i >> 22) & 1, imms, immr, sf, &wm, &tm) < 0) undef(c, i);
            u64 src = X(rn) & msk(sf), dst = opc == 1 ? X(rd) & msk(sf) : 0;
            u64 bot = (dst & ~wm) | (ror_n(src, immr, sf ? 64 : 32) & wm);
            u64 top = opc == 0 ? ((src >> imms) & 1 ? msk(sf) : 0) : opc == 2 ? 0 : dst;
            SET(rd, ((top & ~tm) | (bot & tm)) & msk(sf));
        } else if ((i & 0x1F800000) == 0x13800000) {           /* EXTR */
            int imms = (i >> 10) & 0x3f;
            u64 hi = X(rn) & msk(sf), lo = X(rm) & msk(sf), r;
            int n = sf ? 64 : 32;
            r = imms ? ((lo >> imms) | (hi << (n - imms))) & msk(sf) : lo;
            SET(rd, r);
        } else undef(c, i);
        break;
    }
    case 10: case 11: {                                        /* branches, system */
        if ((i & 0x7C000000) == 0x14000000) {                  /* B / BL */
            if (i >> 31) c->x[30] = pc + 4;
            npc = pc + (u64)(sext(i & 0x3ffffff, 26) << 2);
        } else if ((i & 0x7E000000) == 0x34000000) {           /* CBZ / CBNZ */
            u64 v = X(rd) & msk(sf);
            if ((v == 0) == !((i >> 24) & 1)) npc = pc + (u64)(sext((i >> 5) & 0x7ffff, 19) << 2);
        } else if ((i & 0x7E000000) == 0x36000000) {           /* TBZ / TBNZ */
            int bit = ((i >> 31) << 5) | ((i >> 19) & 31);
            if ((((X(rd) >> bit) & 1) == 0) == !((i >> 24) & 1)) npc = pc + (u64)(sext((i >> 5) & 0x3fff, 14) << 2);
        } else if ((i & 0xFF000010) == 0x54000000) {           /* B.cond */
            if (cond(c, i & 15)) npc = pc + (u64)(sext((i >> 5) & 0x7ffff, 19) << 2);
        } else if ((i & 0xFE000000) == 0xD6000000) {           /* BR / BLR / RET */
            int opc = (i >> 21) & 15; u64 t = X(rn);
            if (opc > 2) undef(c, i);
            if (opc == 1) c->x[30] = pc + 4;
            npc = t;
        } else if ((i & 0xFFE0001F) == 0xD4000001) {           /* SVC */
            c->pc = npc; do_svc(c); return;
        } else if ((i & 0xFF000000) == 0xD4000000) {           /* BRK etc. */
            c->pc = pc; raise(SIGTRAP); return;
        } else if ((i & 0xFFFFFFE0) == 0xD50B7420) {           /* DC ZVA */
            { u64 za = X(rd) & ~63ULL; memset((void*) za, 0, 64); }
        } else if ((i & 0xFFF80000) == 0xD5080000) {           /* SYS (cache maintenance) */
        } else if ((i & 0xFFFFF000) == 0xD5033000 || (i & 0xFFFFF01F) == 0xD503201F || (i & 0xFFFFF000) == 0xD5032000) {
            /* barriers / hints (NOP, YIELD, PAC/BTI hints) */
        } else if ((i & 0xFFF00000) == 0xD5300000) {           /* MRS */
            u32 key = (i >> 5) & 0x7fff; u64 v = 0;
            switch (key) {
            case 0x5E82: v = c->tpidr; break;                  /* TPIDR_EL0 */
            case 0x5A10: v = (u64) c->nzcv << 28; break;       /* NZCV */
            case 0x5A20: v = c->fpcr; break;                   /* FPCR */
            case 0x5A21: v = c->fpsr; break;                   /* FPSR */
            case 0x5F02: { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); v = (u64) ts.tv_sec * 1000000000ULL + (u64) ts.tv_nsec; break; }   /* CNTVCT_EL0 */
            case 0x5F00: v = 1000000000ULL; break;             /* CNTFRQ_EL0 */
            case 0x5807: v = 4; break;                         /* DCZID_EL0: 64-byte blocks */
            case 0x5801: v = 0x84448004ULL; break;             /* CTR_EL0 */
            default: undef(c, i);
            }
            SET(rd, v);
        } else if ((i & 0xFFF00000) == 0xD5100000) {           /* MSR */
            u32 key = (i >> 5) & 0x7fff; u64 v = X(rd);
            switch (key) {
            case 0x5E82: c->tpidr = v; break;
            case 0x5A10: c->nzcv = (u32)(v >> 28) & 15; break;
            case 0x5A20: c->fpcr = v; break;
            case 0x5A21: c->fpsr = v; break;
            default: undef(c, i);
            }
        } else if ((i & 0xFFF8F01F) == 0xD500401F) {           /* MSR (immediate) - ignore */
        } else undef(c, i);
        break;
    }
    case 4: case 6: case 12: case 14: {                        /* loads and stores */
        c->pc = pc;
        if (ldst(c, i) < 0) undef(c, i);
        break;
    }
    case 5: case 13: {                                         /* data processing: register */
        if ((i & 0x1F000000) == 0x0A000000) {                  /* logical shifted */
            int opc = (i >> 29) & 3, n = (i >> 21) & 1;
            u64 b = shiftv(X(rm), (i >> 22) & 3, (i >> 10) & 0x3f, sf); if (n) b = ~b;
            u64 a = X(rn) & msk(sf), r;
            if (!sf && ((i >> 10) & 0x3f) >= 32) undef(c, i);
            r = opc == 1 ? a | b : opc == 2 ? a ^ b : a & b;
            r &= msk(sf);
            if (opc == 3) c->nzcv = (u32)(((r >> (sf ? 63 : 31)) & 1) << 3 | (r == 0) << 2);
            SET(rd, r);
        } else if ((i & 0x1F200000) == 0x0B000000) {           /* add/sub shifted */
            int sub = (i >> 30) & 1, S = (i >> 29) & 1, ty = (i >> 22) & 3;
            if (ty == 3 || (!sf && ((i >> 10) & 0x3f) >= 32)) undef(c, i);
            u64 b = shiftv(X(rm), ty, (i >> 10) & 0x3f, sf);
            SET(rd, addsub(c, sf, X(rn), b, sub, S));
        } else if ((i & 0x1F200000) == 0x0B200000) {           /* add/sub extended */
            int sub = (i >> 30) & 1, S = (i >> 29) & 1, sh = (i >> 10) & 7;
            u64 b = extendv(X(rm), (i >> 13) & 7, sh);
            u64 r = addsub(c, sf, XS(rn), b, sub, S);
            if (S) SET(rd, r); else SETS(rd, r);
        } else if ((i & 0x1FE00000) == 0x1A000000) {           /* ADC / SBC */
            int sub = (i >> 30) & 1, S = (i >> 29) & 1, cin = (c->nzcv >> 1) & 1;
            u64 b = X(rm); if (sub) b = ~b;
            SET(rd, adc(c, sf, X(rn), b, cin, S));
        } else if ((i & 0x1FE00000) == 0x1A400000) {           /* CCMP / CCMN */
            int sub = (i >> 30) & 1;
            if (cond(c, (i >> 12) & 15)) {
                u64 b = ((i >> 11) & 1) ? (u64)((i >> 16) & 31) : X(rm);
                addsub(c, sf, X(rn), b, sub, 1);
            } else c->nzcv = i & 15;
        } else if ((i & 0x1FE00000) == 0x1A800000) {           /* CSEL family */
            int op = (i >> 30) & 1, o2 = (i >> 10) & 1;
            u64 r;
            if (cond(c, (i >> 12) & 15)) r = X(rn);
            else { u64 b = X(rm); r = op ? (o2 ? -b : ~b) : (o2 ? b + 1 : b); }
            SET(rd, r & msk(sf));
        } else if ((i & 0x5FE00000) == 0x1AC00000) {           /* 2-source */
            int op = (i >> 10) & 0x3f; u64 a = X(rn) & msk(sf), b = X(rm) & msk(sf), r;
            switch (op) {
            case 2: if (!b) r = 0; else r = a / b; break;
            case 3: if (!b) r = 0; else if (sf) r = (s64) b == -1 ? -a : (u64)((s64) a / (s64) b); else r = (u64)(s32) ((s32) b == -1 ? (s32)(0u - (u32) a) : (s32) a / (s32) b) & M32; break;
            case 8: r = shiftv(a, 0, (int) b, sf); break;
            case 9: r = shiftv(a, 1, (int) b, sf); break;
            case 10: r = shiftv(a, 2, (int) b, sf); break;
            case 11: r = shiftv(a, 3, (int) b, sf); break;
            default: undef(c, i); r = 0; }
            SET(rd, r & msk(sf));
        } else if ((i & 0x5FE00000) == 0x5AC00000) {          /* 1-source */
            int op = (i >> 10) & 0x3f; u64 a = X(rn) & msk(sf), r = 0; int n = sf ? 64 : 32;
            switch (op) {
            case 0: for (int k = 0; k < n; k++) if (a >> k & 1) r |= 1ULL << (n - 1 - k); break;
            case 1: for (int k = 0; k < n / 16; k++) r |= (rev_n((a >> (16 * k)) & 0xffff, 2)) << (16 * k); break;
            case 2: if (sf) { for (int k = 0; k < 2; k++) r |= rev_n((a >> (32 * k)) & M32, 4) << (32 * k); } else r = rev_n(a, 4); break;
            case 3: if (!sf) undef(c, i); r = rev_n(a, 8); break;
            case 4: r = a ? (u64)(sf ? __builtin_clzll(a) : __builtin_clz((u32) a)) : (u64) n; break;
            case 5: { u64 sg = (a >> (n - 1)) & 1; int k = 1; while (k < n && ((a >> (n - 1 - k)) & 1) == sg) k++; r = (u64)(k - 1); break; }
            default: undef(c, i);
            }
            SET(rd, r & msk(sf));
        } else if ((i & 0x1F000000) == 0x1B000000) {           /* 3-source */
            int o0 = (i >> 15) & 1, op31 = (i >> 21) & 7, ra = (i >> 10) & 31; u64 r;
            u64 a = X(ra), n = X(rn), m = X(rm);
            switch (op31) {
            case 0: { u64 p = (n & msk(sf)) * (m & msk(sf)); r = o0 ? a - p : a + p; break; }
            case 1: { u64 p = (u64)((s64)(s32) n * (s64)(s32) m); r = o0 ? a - p : a + p; sf = 1; break; }
            case 5: { u64 p = (n & M32) * (m & M32); r = o0 ? a - p : a + p; sf = 1; break; }
            case 2: r = (u64)(((__int128)(s64) n * (s64) m) >> 64); sf = 1; break;
            case 6: r = (u64)(((unsigned __int128) n * m) >> 64); sf = 1; break;
            default: undef(c, i); r = 0; }
            SET(rd, r & msk(sf));
        } else undef(c, i);
        break;
    }
    case 7: case 15: {                                         /* SIMD & FP */
        c->pc = pc;
        if ((i & 0x5F000000) == 0x1E000000 || (i & 0xFF000000) == 0x1F000000) { if (fp_insn(c, i) < 0) undef(c, i); }
        else if (simd_insn(c, i) < 0) undef(c, i);
        break;
    }
    default: undef(c, i);
    }
    c->pc = npc;
}

static void run(CPU*c) {
    if (sigsetjmp(g_jb, 1)) {                                  /* host fault with a guest handler */
        int s = g_fault; sigset_t ss; sigemptyset(&ss); sigaddset(&ss, s); sigprocmask(SIG_UNBLOCK, &ss, NULL);
        siginfo_t si; memset(&si, 0, sizeof si); si.si_signo = s; pinfo[s] = si;
        deliver_sig(c, s);
    }
    for (;;) { if (pend) check_pending(c); step(c); }
}

/* ------------------------------------------------------------ ELF loading */
typedef struct { u64 entry, phdr, phnum, base; char interp[4096]; int has_interp; } ElfInfo;

static int load_elf(const char*path, u64 hint, ElfInfo*ei) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) { fprintf(stderr, "pemu: %s: %s\n", path, strerror(errno)); return -1; }
    Elf64_Ehdr eh;
    if (pread(fd, &eh, sizeof eh, 0) != sizeof eh || memcmp(eh.e_ident, "\177ELF", 4) || eh.e_ident[EI_CLASS] != ELFCLASS64 || eh.e_machine != EM_AARCH64) {
        fprintf(stderr, "pemu: %s: not an aarch64 ELF executable\n", path); close(fd); return -1;
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
        void*m = mmap(hint ? (void*) hint : NULL, hi - lo, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | (hint ? MAP_FIXED_NOREPLACE : 0), -1, 0);
        if (m == MAP_FAILED) { perror("pemu: mmap"); return -1; }
        base = (u64) m - lo;
    }
    u64 phdr_addr = 0;
    for (int k = 0; k < eh.e_phnum; k++) {
        Elf64_Phdr*p = &ph[k];
        if (p->p_type == PT_INTERP && pread(fd, ei->interp, p->p_filesz < 4095 ? p->p_filesz : 4095, (off_t) p->p_offset) > 0) ei->has_interp = 1;
        if (p->p_type == PT_PHDR) phdr_addr = base + p->p_vaddr;
        if (p->p_type != PT_LOAD) continue;
        u64 s = (base + p->p_vaddr) & ~4095ULL, e = (base + p->p_vaddr + p->p_memsz + 4095) & ~4095ULL;
        void*m = mmap((void*) s, e - s, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | (eh.e_type == ET_EXEC ? 0 : 0), -1, 0);
        if (m == MAP_FAILED) { perror("pemu: mmap segment"); return -1; }
        if (p->p_filesz && pread(fd, (void*)(base + p->p_vaddr), p->p_filesz, (off_t) p->p_offset) != (ssize_t) p->p_filesz) { fprintf(stderr, "pemu: short read\n"); return -1; }
        int prot = ((p->p_flags & PF_R) ? PROT_READ : 0) | ((p->p_flags & PF_W) ? PROT_WRITE : 0) | ((p->p_flags & PF_X) ? PROT_EXEC : 0);
        mprotect((void*) s, e - s, prot | PROT_READ);
        if (!phdr_addr && p->p_offset <= eh.e_phoff && eh.e_phoff < p->p_offset + p->p_filesz) phdr_addr = base + p->p_vaddr + (eh.e_phoff - p->p_offset);
    }
    ei->entry = base + eh.e_entry; ei->phdr = phdr_addr; ei->phnum = eh.e_phnum; ei->base = base;
    free(ph); close(fd);
    return 0;
}

static u64 push_str(u64*sp, const char*s) { size_t n = strlen(s) + 1; *sp -= n; memcpy((void*)*sp, s, n); return *sp; }

int main(int argc, char**argv, char**envp) {
    const char*argv0 = NULL; int a = 1;
    for (; a < argc && argv[a][0] == '-' && argv[a][1]; a++) {
        const char*o = argv[a];
        if (!strcmp(o, "--")) { a++; break; }
        if (!strcmp(o, "-0") && a + 1 < argc) argv0 = argv[++a];
        else if ((!strcmp(o, "-L") || !strcmp(o, "-cpu") || !strcmp(o, "-s") || !strcmp(o, "-g") || !strcmp(o, "-R")) && a + 1 < argc) a++;
        else if (!strcmp(o, "-E") && a + 1 < argc) { char*v = strdup(argv[++a]); char*eq = strchr(v, '='); if (eq) { *eq = 0; setenv(v, eq + 1, 1); } }
        else if (!strcmp(o, "-U") && a + 1 < argc) unsetenv(argv[++a]);
        else if (!strcmp(o, "-strace")) g_trace = 1;
        else if (!strcmp(o, "--version") || !strcmp(o, "-version")) { puts("pemu 0.1 (aarch64 user-mode emulator, qemu-user alternative for prx)"); return 0; }
        else { fprintf(stderr, "pemu: unknown option %s\n", o); return 2; }
    }
    if (a >= argc) { fprintf(stderr, "usage: pemu [-0 argv0] [-strace] program [args...]\n"); return 2; }
    if (getenv("QEMU_STRACE")) g_trace = 1;
    ssize_t sl = readlink("/proc/self/exe", g_self, sizeof g_self - 1);
    if (sl > 0) g_self[sl] = 0; else strcpy(g_self, argv[0]);
    (void) envp;
    const char*prog = argv[a];
    /* guest environment = host environment with QEMU_SET_ENV / QEMU_UNSET_ENV applied */
    char*sete = getenv("QEMU_SET_ENV"); if (sete) { sete = strdup(sete); for (char*t = strtok(sete, ","); t; t = strtok(NULL, ",")) { char*eq = strchr(t, '='); if (eq) { *eq = 0; setenv(t, eq + 1, 1); } } }
    char*unse = getenv("QEMU_UNSET_ENV"); if (unse) { unse = strdup(unse); for (char*t = strtok(unse, ","); t; t = strtok(NULL, ",")) unsetenv(t); }
    init_tbl();
    ElfInfo prg = {0}, itp = {0};
    if (load_elf(prog, 0, &prg) < 0) return 127;
    u64 entry = prg.entry;
    if (prg.has_interp && load_elf(prg.interp, 0, &itp) < 0) return 127;
    if (prg.has_interp) entry = itp.entry;
    /* brk region */
    brk_end = (u64) mmap(NULL, 1ULL << 30, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    brk_base = brk_cur = brk_end; brk_end += 1ULL << 30;
    /* signal trampoline page */
    g_tramp = (u64) mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ((u32*) g_tramp)[0] = 0xD2801168; ((u32*) g_tramp)[1] = 0xD4000001;
    /* host fault handlers */
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = hsegv; sa.sa_flags = SA_SIGINFO | SA_NODEFER; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
    /* stack */
    size_t ssz = 8UL << 20;
    u64 top = (u64) mmap(NULL, ssz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_STACK, -1, 0) + ssz;
    u64 sp = top;
    u64 execfn = push_str(&sp, prog);
    int ac = argc - a, ec = 0; while (environ[ec]) ec++;
    u64*as = calloc((size_t) ac + 1, 8), *es = calloc((size_t) ec + 1, 8);
    for (int k = ec - 1; k >= 0; k--) es[k] = push_str(&sp, environ[k]);
    for (int k = ac - 1; k >= 0; k--) as[k] = push_str(&sp, k == 0 && argv0 ? argv0 : argv[a + k]);
    u64 plat = push_str(&sp, "aarch64");
    sp &= ~15ULL; sp -= 16; u64 rnd = sp; if (getrandom((void*) rnd, 16, 0) < 0) memset((void*) rnd, 0x5a, 16);
    u64 aux[][2] = {
        {AT_PHDR, prg.phdr}, {AT_PHENT, sizeof(Elf64_Phdr)}, {AT_PHNUM, prg.phnum}, {AT_PAGESZ, 4096},
        {AT_BASE, itp.base}, {AT_FLAGS, 0}, {AT_ENTRY, prg.entry}, {AT_UID, getuid()}, {AT_EUID, geteuid()},
        {AT_GID, getgid()}, {AT_EGID, getegid()}, {AT_HWCAP, 3}, {AT_CLKTCK, 100}, {AT_SECURE, 0},
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
    cpu.sp = sp; cpu.pc = entry;
    run(&cpu);
    return 0;
}
