typedef unsigned long size_t;
typedef long ssize_t;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef unsigned long long u64;
typedef long long s64;
#define NULL ((void*)0)
#define EXP __attribute__((visibility("default")))
#define PM 4096
#define AT_FDCWD (-100)
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0x40
#define O_EXCL 0x80
#define O_TRUNC 0x200
#define O_APPEND 0x400
#define O_CLOEXEC 0x80000
#define O_ACCMODE_ 3
#if defined(__i386__)
#define O_LARGEFILE_ 0x8000
#else
#define O_LARGEFILE_ 0
#endif
#if defined(__aarch64__)
#define O_NOFOLLOW 0x8000
#else
#define O_NOFOLLOW 0x20000
#endif
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_SYMLINK_FOLLOW 0x400
#define AT_REMOVEDIR 0x200
#define EPERM 1
#define ENOENT 2
#define ENOEXEC 8
#define EACCES 13
#define EFAULT 14
#define EEXIST 17
#define ENOTDIR 20
#define EINVAL 22
#define ERANGE 34
#define ENAMETOOLONG 36
#define ENOSYS 38
#define ELOOP 40
#define ENOMEM 12
#define EINTR 4
#define ESTALE 116
#define E2BIG 7
#define EOPNOTSUPP 95
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0
#if defined(__aarch64__)
#define N_fchdir 50
#define N_getcwd 17
#define N_pipe2 59
#define N_mknodat 33
#define N_mkdirat 34
#define N_unlinkat 35
#define N_symlinkat 36
#define N_linkat 37
#define N_renameat 38
#define N_truncate 45
#define N_faccessat 48
#define N_chdir 49
#define N_fchmodat 53
#define N_fchownat 54
#define N_openat 56
#define N_close 57
#define N_lseek 62
#define N_read 63
#define N_write 64
#define N_fchmod 52
#define MODE_OFF 16
#define N_pread64 67
#define N_readlinkat 78
#define N_newfstatat 79
#define N_fstat 80
#define N_utimensat 88
#define N_capget 90
#define N_capset 91
#define N_getpid 172
#define N_sched_getaffinity 123
#define N_getppid 173
#define N_getuid 174
#define N_geteuid 175
#define N_getgid 176
#define N_getegid 177
#define N_gettid 178
#define N_execve 221
#define N_renameat2 276
#define N_getrandom 278
#define N_memfd_create 279
#define N_statx 291
#define N_execveat 281
#define N_setxattr 5
#define N_lsetxattr 6
#define N_getxattr 8
#define N_lgetxattr 9
#define N_listxattr 11
#define N_llistxattr 12
#define N_removexattr 14
#define N_lremovexattr 15
#define N_statfs 43
#define N_fstatfs 44
#define N_ioctl 29
#define N_fcntl 25
#define OFF_UID 24
#define OFF_NLINK 20
#define OFF_INO 8
#elif defined(__x86_64__)
#define N_fchdir 81
#define N_read 0
#define N_write 1
#define N_close 3
#define N_fstat 5
#define N_lseek 8
#define N_ioctl 16
#define N_pread64 17
#define N_getpid 39
#define N_sched_getaffinity 204
#define N_execve 59
#define N_fcntl 72
#define N_truncate 76
#define N_getcwd 79
#define N_chdir 80
#define N_fchmod 91
#define MODE_OFF 24
#define N_getuid 102
#define N_getgid 104
#define N_geteuid 107
#define N_getegid 108
#define N_getppid 110
#define N_capget 125
#define N_capset 126
#define N_statfs 137
#define N_fstatfs 138
#define N_gettid 186
#define N_setxattr 188
#define N_lsetxattr 189
#define N_getxattr 191
#define N_lgetxattr 192
#define N_listxattr 194
#define N_llistxattr 195
#define N_removexattr 197
#define N_lremovexattr 198
#define N_openat 257
#define N_mkdirat 258
#define N_mknodat 259
#define N_fchownat 260
#define N_newfstatat 262
#define N_unlinkat 263
#define N_renameat 264
#define N_linkat 265
#define N_symlinkat 266
#define N_readlinkat 267
#define N_fchmodat 268
#define N_faccessat 269
#define N_utimensat 280
#define N_pipe2 293
#define N_renameat2 316
#define N_getrandom 318
#define N_memfd_create 319
#define N_execveat 322
#define N_statx 332
#define OFF_UID 28
#define OFF_NLINK 16
#define OFF_INO 8
#elif defined(__i386__)
/* i386 guest (qemu-user only). Kernel i386 ABI: 32-bit longs, *64 stat family,
   32-bit uid/gid syscalls, 64-bit file offsets passed as lo/hi register pairs. */
#define N_read 3
#define N_write 4
#define N_close 6
#define N_execve 11
#define N_chdir 12
#define N_lseek 19
#define N_getpid 20
#define N_getppid 64
#define N_ioctl 54
#define N_fcntl 55
#define N_truncate 92
#define N_fchmod 94
#define N_statfs 99
#define N_fstatfs 100
#define N_fchdir 133
#define N_pread64 180
#define N_capget 184
#define N_capset 185
#define N_getcwd 183
#define N_truncate64 193
#define N_fstat 197
#define N_getuid 199
#define N_getgid 200
#define N_geteuid 201
#define N_getegid 202
#define N_gettid 224
#define N_setxattr 226
#define N_lsetxattr 227
#define N_getxattr 229
#define N_lgetxattr 230
#define N_listxattr 232
#define N_llistxattr 233
#define N_removexattr 235
#define N_lremovexattr 236
#define N_sched_getaffinity 242
#define N_statfs64 268
#define N_fstatfs64 269
#define N_openat 295
#define N_mkdirat 296
#define N_mknodat 297
#define N_fchownat 298
#define N_newfstatat 300
#define N_unlinkat 301
#define N_renameat 302
#define N_linkat 303
#define N_symlinkat 304
#define N_readlinkat 305
#define N_fchmodat 306
#define N_faccessat 307
#define N_utimensat 320
#define N_pipe2 331
#define N_renameat2 353
#define N_getrandom 355
#define N_memfd_create 356
#define N_execveat 358
#define N_statx 383
#define N_utimensat_time64 412
#define MODE_OFF 16
#define OFF_UID 24
#define OFF_NLINK 20
#define OFF_INO 88
#else
#error "Unsupported target architecture."
#endif
#define N_openat2 437
#define N_faccessat2 439
#if defined(__aarch64__)
static inline long sc(long n, long a, long b, long c, long d, long e, long f) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;
    __asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory", "cc");
    return x0;
}
#elif defined(__i386__)
/* int 0x80 with up to six arguments. ebx (PIC register) and ebp are saved by
   hand; the arguments are loaded from a stack array through eax. */
static inline long sc(long n, long a, long b, long c, long d, long e, long f) {
    long v[7] = {n, a, b, c, d, e, f};
    long ret;
    __asm__ volatile(
        "push %%ebx\n\t"
        "push %%ebp\n\t"
        "mov 4(%%eax), %%ebx\n\t"
        "mov 8(%%eax), %%ecx\n\t"
        "mov 12(%%eax), %%edx\n\t"
        "mov 16(%%eax), %%esi\n\t"
        "mov 20(%%eax), %%edi\n\t"
        "mov 24(%%eax), %%ebp\n\t"
        "mov (%%eax), %%eax\n\t"
        "int $0x80\n\t"
        "pop %%ebp\n\t"
        "pop %%ebx"
        : "=a"(ret) : "a"(v) : "ecx", "edx", "esi", "edi", "memory", "cc");
    return ret;
}
#else
static inline long sc(long n, long a, long b, long c, long d, long e, long f) {
    long ret;
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return ret;
}
#endif
#if defined(__i386__)
/* The shim is linked with -nostdlib, so the 64-bit division/modulo helpers the
   compiler emits on 32-bit x86 are provided here (shift/subtract, only used for
   a few rare formatting paths). */
static u64 udivmod64(u64 n, u64 d, u64*rem) {
    u64 q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r<<1)|((n>>i)&1);
        if (r >= d) {
            r -= d;
            q |= (u64)1<<i;
        }
    }
    if (rem)*rem = r;
    return q;
}
__attribute__((visibility("hidden"))) u64 __udivdi3(u64 a, u64 b) {
    return udivmod64(a, b, 0);
}
__attribute__((visibility("hidden"))) u64 __umoddi3(u64 a, u64 b) {
    u64 r;
    udivmod64(a, b, &r);
    return r;
}
#endif
#define SC3(n,a,b,c) sc((n),(long)(a),(long)(b),(long)(c),0,0,0)
#define SC4(n,a,b,c,d) sc((n),(long)(a),(long)(b),(long)(c),(long)(d),0,0)
#define SC5(n,a,b,c,d,e) sc((n),(long)(a),(long)(b),(long)(c),(long)(d),(long)(e),0)
static long pread_(long fd, void*b, long n, u64 off) {
#if defined(__i386__)
    return sc(N_pread64, fd, (long) b, n, (long)(u32) off, (long)(u32)(off>>32), 0);
#else
    return sc(N_pread64, fd, (long) b, n, (long) off, 0, 0);
#endif
}
extern int*__errno_location(void);
extern void*malloc(size_t);
extern void*realloc(void*, size_t);
extern void free(void*);
extern void*memcpy(void*, const void*, size_t);
extern void*memmove(void*, const void*, size_t);
extern void*memset(void*, int, size_t);
extern int memcmp(const void*, const void*, size_t);
extern size_t strlen(const char*);
extern int strncmp(const char*, const char*, size_t);
extern int strcmp(const char*, const char*);
extern char*getenv(const char*);
extern void*dlsym(void*, const char*);
extern char**environ;
extern int waitpid(int, int*, int);
extern int pipe2(int*, int);
extern void*fdopen(int, const char*);
extern int fclose(void*);
extern int fileno(void*);
extern void*signal(int, void*);
extern int socket(int, int, int);
extern int connect(int, const void*, unsigned);
extern int send(int, const void*, size_t, int);
extern long recv(int, void*, size_t, int);
extern int setsockopt(int, int, int, const void*, unsigned);
extern int posix_spawn_file_actions_init(void*);
extern int posix_spawn_file_actions_adddup2(void*, int, int);
extern int posix_spawn_file_actions_destroy(void*);
extern int posix_spawnattr_init(void*);
extern int posix_spawnattr_destroy(void*);
extern int posix_spawnattr_setsigdefault(void*, const void*);
extern int posix_spawnattr_setflags(void*, short);
extern int sigemptyset(void*);
extern int sigaddset(void*, int);
#define RTLD_NEXT ((void*)-1L)
static void*nextsym(const char*n) {
    return dlsym(RTLD_NEXT, n);
}
static long ret(long r) {
    if (r<0 && r>-4096) {
        *__errno_location() = (int)-r;
        return-1;
    }
    return r;
}
static long fail(int e) {
    *__errno_location() = e;
    return-1;
}
static int geterr(void) {
    return*__errno_location();
}
static char g_root[PM];
static size_t g_rl;
static char g_shim[PM];
#define LPM 65536
static char g_libpath[LPM];
static char g_loader[PM];
static char g_qemu[PM];
#define BENV 16384
static char g_bindenv[BENV];
static int g_noargv0;
/* comma-separated list of IPv4 and/or IPv6 literals */
static char g_dns[512] = "8.8.8.8,1.1.1.1";
static char g_self_exe[PM];
static int g_fake;
static int g_trace;
static int g_ncpu;
static u64 g_memkb;
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
static u64 parse_u64(const char*s) {
    u64 v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v*10+(u64)(*s-'0');
        s++;
    }
    return v;
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
        if (g_ncpu > 0) {
            memset(m, 0, cpusetsize);
            for (int i = 0; i<g_ncpu && (size_t)(i/8)<cpusetsize; i++) m[i/8] |= (u8)(1u<<(i%8));
        } else {
            memset(m, 0xff, cpusetsize);
        }
        return 0;
    }
    size_t got = (size_t) r < cpusetsize ? (size_t) r : cpusetsize;
    if (got<cpusetsize) memset(m+got, 0, cpusetsize-got);
    if (g_ncpu>0) {
        int left = g_ncpu;
        for (size_t b = 0; b<got*8; b++) {
            if (m[b/8]&(1u<<(b%8))) {
                if (left>0) left--;
                else m[b/8] &= (u8)~(1u<<(b%8));
            }
        }
        if (left == g_ncpu) {
            memset(m, 0, cpusetsize);
            for (int i = 0; i<g_ncpu && (size_t)(i/8)<cpusetsize; i++) m[i/8] |= (u8)(1u<<(i%8));
        }
    }
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
        else if (envmatch(s, "PRX_CPUS")) {
            u64 v = parse_u64(s+9);
            g_ncpu = v>4096 ? 4096 : (int) v;
        }
        else if (envmatch(s, "PRX_MEM_KB")) g_memkb = parse_u64(s+11);
        else if (envmatch(s, "PRX_VUSER")) copyval(g_vuser, sizeof g_vuser, s+10);
        else if (envmatch(s, "PRX_CRED")) copyval(g_credbuf, sizeof g_credbuf, s+9);
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
__attribute__((constructor)) static void prx_init(int argc, char**argv, char**envp) {
    (void) argc;
    (void) argv;
    prx_do_init(envp);
}
/* LD_PRELOAD constructors run after the constructors of the app's own libs
   (jemalloc, ...). Interposed entry points call this so the SIGSYS handler
   and path translation are active before those constructors do real work. */
static void prx_early(void) {
    if (!g_inited) prx_do_init(environ);
}
static int in_bind(const char*g) {
    int best = 0;
    size_t best_len = 0;
    for (int i = 0; i<g_nb; i++) {
        size_t l = g_bindlen[i];
        if (l>best_len && strncmp(g, g_bind[i], l) == 0 && (g[l] == 0 || g[l] == '/')) {
            best = i+1;
            best_len = l;
        }
    }
    return best;
}
static int hasroot(const char*p) {
    return g_rl && strncmp(p, g_root, g_rl) == 0 && (p[g_rl] == 0 || p[g_rl] == '/');
}
static int bind_to_host(int i, const char*res, char*buf) {
    const char*rest = res+g_bindlen[i];
    size_t hl = g_bhlen[i], rl = strlen(rest);
    if (hl+rl >= PM) {
        fail(ENAMETOOLONG);
        return-1;
    }
    memcpy(buf, g_bhost[i], hl);
    memcpy(buf+hl, rest, rl+1);
    return 0;
}
static const char*rev_bind(const char*host, char*out) {
    int best = -1;
    size_t best_hl = 0;
    for (int i = 0; i<g_nb; i++) {
        if (g_bid[i]) continue;
        size_t hl = g_bhlen[i];
        if (hl>best_hl && strncmp(host, g_bhost[i], hl) == 0 && (host[hl] == 0 || host[hl] == '/')) {
            best = i;
            best_hl = hl;
        }
    }
    if (best >= 0) {
        size_t gl = g_bindlen[best], rl = strlen(host+best_hl);
        if (gl+rl >= PM) return NULL;
        memcpy(out, g_bind[best], gl);
        memcpy(out+gl, host+best_hl, rl+1);
        return out;
    }
    return NULL;
}
static void fdpath(char*out, int fd) {
    static const char pre[] = "/proc/self/fd/";
    memcpy(out, pre, sizeof pre-1);
    char*o = out+sizeof pre-1;
    char t[12];
    int n = 0;
    unsigned v = fd<0 ? 0 : (unsigned) fd;
    do {
        t[n++] = '0'+v%10;
        v /= 10;
    } while (v);
    while (n)*o++ = t[--n];
    *o = 0;
}
static int is_proc_exe(const char*p) {
    if (!p) return 0;
    if (strcmp(p, "/proc/self/exe") == 0 || strcmp(p, "/proc/thread-self/exe") == 0) return 1;
    if (strncmp(p, "/proc/", 6) == 0) {
        const char*q = p+6;
        u32 pid = 0;
        while (*q >= '0' && *q <= '9') pid = pid*10+(u32)(*q++-'0');
        if (q>p+6 && strcmp(q, "/exe") == 0) {
            u32 mypid = (u32) sc(N_getpid, 0, 0, 0, 0, 0, 0);
            if (pid == mypid) return 1;
        }
    }
    return 0;
}
static int has_dotdot(const char*s) {
    while (*s) {
        while (*s == '/') s++;
        const char*t = s;
        while (*s && *s != '/') s++;
        if (s-t == 2 && t[0] == '.' && t[1] == '.') return 1;
    }
    return 0;
}
static int mkabs(int dfd, const char*in, char*g) {
    size_t il = strlen(in);
    if (in[0] == '/') {
        if (hasroot(in)) return 1;
        int bi = in_bind(in);
        if (bi && g_bid[bi-1] && !has_dotdot(in)) return 1;
        if (il >= PM) {
            fail(ENAMETOOLONG);
            return-1;
        }
        memcpy(g, in, il+1);
        return 0;
    }
    char base[PM];
    long n;
    if (dfd == AT_FDCWD) {
        n = sc(N_getcwd, (long) base, PM, 0, 0, 0, 0);
    } else {
        char l[32];
        fdpath(l, dfd);
        n = sc(N_readlinkat, AT_FDCWD, (long) l, (long) base, PM-1, 0, 0);
        if (n>0) base[n] = 0;
    }
    if (n <= 0) return 1;
    const char*b;
    char rb[PM];
    int ib;
    if (hasroot(base)) {
        b = base+g_rl;
        if (!*b) b = "/";
    } else if ((ib = in_bind(base)) && g_bid[ib-1]) {
        if (!has_dotdot(in)) return 1;
        b = base;
    } else if (!(b = rev_bind(base, rb))) {
        return 1;
    }
    size_t bl = strlen(b);
    if (bl+il+2 >= PM) {
        fail(ENAMETOOLONG);
        return-1;
    }
    memcpy(g, b, bl);
    if (g[bl-1] != '/') g[bl++] = '/';
    memcpy(g+bl, in, il+1);
    return 0;
}
static int canon(const char*gin, char*res, int follow, int*bind) {
    char rest[PM], lnk[PM], host[PM];
    size_t rl = 1;
    int links = 0;
    res[0] = '/';
    res[1] = 0;
    *bind = 0;
    size_t n = strlen(gin);
    memcpy(rest, gin, n+1);
    char*p = rest;
    for (;; ) {
        while (*p == '/') p++;
        if (!*p) break;
        char*e = p;
        while (*e && *e != '/') e++;
        size_t cl = (size_t)(e-p);
        char*q = e;
        while (*q == '/') q++;
        int last = (*q == 0);
        int has_trailing = (*e == '/');
        if (cl == 1 && p[0] == '.') {
            p = e;
            continue;
        }
        if (cl == 2 && p[0] == '.' && p[1] == '.') {
            if (rl>1) {
                size_t i = rl-1;
                while (res[i] != '/') i--;
                rl = i ? i : 1;
                res[rl] = 0;
            }
            p = e;
            continue;
        }
        if (g_rl+rl+1+cl >= PM) {
            fail(ENAMETOOLONG);
            return-1;
        }
        size_t save = rl;
        if (rl>1) res[rl++] = '/';
        memcpy(res+rl, p, cl);
        rl += cl;
        res[rl] = 0;
        p = e;
        int bi = in_bind(res);
        if (bi && g_bid[bi-1]) {
            if (has_dotdot(p)) continue;
            size_t tl = strlen(p);
            if (rl+tl >= PM) {
                fail(ENAMETOOLONG);
                return-1;
            }
            memcpy(res+rl, p, tl+1);
            *bind = bi;
            return 0;
        }
        if (!last || follow || has_trailing) {
            if (bi) {
                if (bind_to_host(bi-1, res, host)<0) return-1;
            } else {
                memcpy(host, g_root, g_rl);
                memcpy(host+g_rl, res, rl+1);
            }
            long r = sc(N_readlinkat, AT_FDCWD, (long) host, (long) lnk, PM-1, 0, 0);
            if (r>0) {
                if (++links>40) {
                    fail(ELOOP);
                    return-1;
                }
                lnk[r] = 0;
                rl = save;
                res[rl] = 0;
                if (lnk[0] == '/') {
                    rl = 1;
                    res[1] = 0;
                }
                size_t tl = strlen(p), ll = (size_t) r;
                if (ll+1+tl >= PM) {
                    fail(ENAMETOOLONG);
                    return-1;
                }
                memmove(rest+ll+1, p, tl+1);
                memcpy(rest, lnk, ll);
                rest[ll] = '/';
                p = rest;
            }
        }
    }
    *bind = in_bind(res);
    return 0;
}
static int tr(int dfd, const char*in, char*buf, int follow, const char**out) {
    *out = in;
    if (!g_inited) prx_early();
    if (!g_rl || !in || !*in) return 0;
    if (g_self_exe[0] && follow && is_proc_exe(in)) in = g_self_exe;
    char g[PM], res[PM];
    int bind;
    int m = mkabs(dfd, in, g);
    if (m>0) return 0;
    if (m<0) return-1;
    if (canon(g, res, follow, &bind)<0) return-1;
    size_t rl = strlen(res);
    if (bind) {
        if (bind_to_host(bind-1, res, buf)<0) return-1;
    } else {
        if (g_rl+rl >= PM) {
            fail(ENAMETOOLONG);
            return-1;
        }
        memcpy(buf, g_root, g_rl);
        if (rl>1) memcpy(buf+g_rl, res, rl+1);
        else buf[g_rl] = 0;
    }
    *out = buf;
    return 0;
}
#define TR1(dfd,p,fl) char b1_[PM];const char*h1;if(tr((dfd),(p),b1_,(fl),&h1)<0)return-1;
static int fake_proc_file(const char*content, size_t len) {
    long fd = sc(N_memfd_create, (long) "fake_proc", 1, 0, 0, 0, 0);
    if (fd<0) {
        int pfd[2];
        if (sc(N_pipe2, (long) pfd, O_CLOEXEC, 0, 0, 0, 0) == 0) {
            sc(N_write, pfd[1], (long) content, (long) len, 0, 0, 0);
            sc(N_close, pfd[1], 0, 0, 0, 0, 0);
            return pfd[0];
        }
        return-1;
    }
    sc(N_write, fd, (long) content, (long) len, 0, 0, 0);
    sc(N_lseek, fd, 0, 0, 0, 0, 0);
    return (int) fd;
}
struct fb {
    int fd;
    size_t n;
    char b[4096];
};
static void fb_flush(struct fb*f) {
    size_t o = 0;
    while (o<f->n) {
        long k = sc(N_write, f->fd, (long)(f->b+o), (long)(f->n-o), 0, 0, 0);
        if (k <= 0) break;
        o += (size_t) k;
    }
    f->n = 0;
}
static void fb_put(struct fb*f, const char*s, size_t n) {
    while (n) {
        size_t room = sizeof f->b-f->n;
        size_t k = n<room ? n : room;
        memcpy(f->b+f->n, s, k);
        f->n += k;
        s += k;
        n -= k;
        if (f->n == sizeof f->b) fb_flush(f);
    }
}
static void fb_str(struct fb*f, const char*s) {
    fb_put(f, s, strlen(s));
}
static size_t u64_dec(char*o, u64 v) {
    char t[24];
    size_t k = 0, n = 0;
    do {
        t[k++] = (char)('0'+v%10);
        v /= 10;
    } while (v);
    while (k) o[n++] = t[--k];
    o[n] = 0;
    return n;
}
static void fb_num(struct fb*f, u64 v) {
    char t[24];
    size_t n = u64_dec(t, v);
    fb_put(f, t, n);
}
static void fb_pad(struct fb*f, size_t n) {
    while (n--) fb_put(f, " ", 1);
}
static int fake_begin(struct fb*f) {
    long fd = sc(N_memfd_create, (long) "fake_proc", 1, 0, 0, 0, 0);
    if (fd<0) return -1;
    f->fd = (int) fd;
    f->n = 0;
    return 0;
}
static int fake_end(struct fb*f) {
    fb_flush(f);
    sc(N_lseek, f->fd, 0, 0, 0, 0, 0);
    return f->fd;
}
struct lr {
    int fd;
    size_t pos, len;
    char b[4096];
};
static int lr_line(struct lr*r, char*o, size_t cap) {
    size_t n = 0;
    int got = 0;
    for (;;) {
        if (r->pos >= r->len) {
            long k = sc(N_read, r->fd, (long) r->b, (long) sizeof r->b, 0, 0, 0);
            if (k <= 0) break;
            r->len = (size_t) k;
            r->pos = 0;
        }
        got = 1;
        char c = r->b[r->pos++];
        if (c == '\n') {
            o[n] = 0;
            return (int) n;
        }
        if (n+1<cap) o[n++] = c;
    }
    if (!got) return-1;
    o[n] = 0;
    return (int) n;
}
static u64 line_val(const char*s) {
    while (*s && *s != ':') s++;
    if (*s == ':') s++;
    while (*s == ' ' || *s == '\t') s++;
    return parse_u64(s);
}
static u64 umin(u64 a, u64 b) {
    return a<b ? a : b;
}
static int fake_cpurange(void) {
    struct fb f;
    if (fake_begin(&f)<0) return-1;
    if (g_ncpu>1) {
        fb_str(&f, "0-");
        fb_num(&f, (u64)(g_ncpu-1));
    } else fb_str(&f, "0");
    fb_str(&f, "\n");
    return fake_end(&f);
}
static int fake_stat(void) {
    static const u64 v[10] = {21200, 450, 12300, 954000, 3200, 120, 80, 0, 0, 0};
    struct fb f;
    if (fake_begin(&f)<0) return-1;
    fb_str(&f, "cpu ");
    for (int k = 0; k<10; k++) {
        fb_str(&f, " ");
        fb_num(&f, v[k]);
    }
    fb_str(&f, "\n");
    for (int i = 0; i<g_ncpu; i++) {
        fb_str(&f, "cpu");
        fb_num(&f, (u64) i);
        for (int k = 0; k<10; k++) {
            fb_str(&f, " ");
            fb_num(&f, v[k]/(u64) g_ncpu);
        }
        fb_str(&f, "\n");
    }
    fb_str(&f, "intr 1234567 10 20 0 0 0\nctxt 5678901\nbtime 1680000000\nprocesses 25000\nprocs_running 1\nprocs_blocked 0\n");
    return fake_end(&f);
}
static int fake_cpuinfo(void) {
    struct fb f;
    if (fake_begin(&f)<0) return-1;
    int n = g_ncpu, blk = -1, inb = 0;
    long hfd = sc(N_openat, AT_FDCWD, (long) "/proc/cpuinfo", O_RDONLY|O_CLOEXEC, 0, 0, 0);
    if (hfd >= 0) {
        struct lr r;
        char ln[8192];
        int len;
        r.fd = (int) hfd;
        r.pos = r.len = 0;
        while ((len = lr_line(&r, ln, sizeof ln)) >= 0) {
            if (len >= 9 && strncmp(ln, "processor", 9) == 0 && (ln[9] == ' ' || ln[9] == '\t' || ln[9] == ':')) {
                blk++;
                inb = 1;
            } else if (len == 0 && inb) {
                inb = 0;
                if (blk<n) fb_str(&f, "\n");
                continue;
            }
            if (inb && blk >= n) continue;
            if (inb) {
                const char*col = ln;
                while (*col && *col != ':') col++;
                if (*col == ':') {
                    size_t kl = (size_t)(col-ln), tk = kl;
                    u64 val = 0;
                    int fix = 0;
                    while (tk>0 && (ln[tk-1] == ' ' || ln[tk-1] == '\t')) tk--;
                    if (tk == 9 && strncmp(ln, "processor", 9) == 0) { val = (u64) blk; fix = 1; }
                    else if (tk == 7 && strncmp(ln, "core id", 7) == 0) { val = (u64) blk; fix = 1; }
                    else if (tk == 8 && strncmp(ln, "siblings", 8) == 0) { val = (u64) n; fix = 1; }
                    else if (tk == 9 && strncmp(ln, "cpu cores", 9) == 0) { val = (u64) n; fix = 1; }
                    if (fix) {
                        fb_put(&f, ln, kl+1);
                        fb_str(&f, " ");
                        fb_num(&f, val);
                        fb_str(&f, "\n");
                        continue;
                    }
                }
            }
            fb_put(&f, ln, (size_t) len);
            fb_str(&f, "\n");
        }
        sc(N_close, hfd, 0, 0, 0, 0, 0);
    }
    for (int i = blk+1; i<n; i++) {
        fb_str(&f, "processor\t: ");
        fb_num(&f, (u64) i);
        fb_str(&f, "\nmodel name\t: Virtual CPU\n\n");
    }
    return fake_end(&f);
}
static int fake_meminfo(void) {
    char hb[16384];
    size_t hn = 0;
    long hfd = sc(N_openat, AT_FDCWD, (long) "/proc/meminfo", O_RDONLY|O_CLOEXEC, 0, 0, 0);
    if (hfd<0) return-1;
    for (;;) {
        long k = sc(N_read, hfd, (long)(hb+hn), (long)(sizeof hb-1-hn), 0, 0, 0);
        if (k <= 0) break;
        hn += (size_t) k;
        if (hn >= sizeof hb-1) break;
    }
    sc(N_close, hfd, 0, 0, 0, 0, 0);
    hb[hn] = 0;
    if (!hn) return-1;
    u64 L = g_memkb, hfree = 0, havail = 0, hbuf = 0, hcache = 0, hsrec = 0;
    int have_avail = 0;
    for (char*p = hb; *p;) {
        char*e = p;
        while (*e && *e != '\n') e++;
        char sv = *e;
        *e = 0;
        if (strncmp(p, "MemFree:", 8) == 0) hfree = line_val(p);
        else if (strncmp(p, "MemAvailable:", 13) == 0) {
            havail = line_val(p);
            have_avail = 1;
        } else if (strncmp(p, "Buffers:", 8) == 0) hbuf = line_val(p);
        else if (strncmp(p, "Cached:", 7) == 0) hcache = line_val(p);
        else if (strncmp(p, "SReclaimable:", 13) == 0) hsrec = line_val(p);
        *e = sv;
        p = sv ? e+1 : e;
    }
    if (!have_avail) havail = hfree+hbuf+hcache;
    u64 avail = umin(havail, L), fre = umin(hfree, avail);
    u64 buf = umin(hbuf, avail-fre), cache = umin(hcache, avail-fre-buf), srec;
    srec = umin(hsrec, avail-fre-buf-cache);
    struct fb f;
    if (fake_begin(&f)<0) return-1;
    for (char*p = hb; *p;) {
        char*e = p;
        while (*e && *e != '\n') e++;
        char sv = *e;
        *e = 0;
        size_t ll = (size_t)(e-p);
        const char*col = p;
        while (*col && *col != ':') col++;
        if (*col == ':' && ll>=3 && strcmp(e-3, " kB") == 0) {
            size_t kl = (size_t)(col-p)+1;
            u64 val = line_val(p);
            int skip = strncmp(p, "Swap", 4) == 0 || strncmp(p, "Commit", 6) == 0 || strncmp(p, "Vmalloc", 7) == 0 || strncmp(p, "Huge", 4) == 0 || strncmp(p, "DirectMap", 9) == 0;
            if (strncmp(p, "MemTotal:", 9) == 0) val = L;
            else if (strncmp(p, "MemFree:", 8) == 0) val = fre;
            else if (strncmp(p, "MemAvailable:", 13) == 0) val = avail;
            else if (strncmp(p, "Buffers:", 8) == 0) val = buf;
            else if (strncmp(p, "Cached:", 7) == 0) val = cache;
            else if (strncmp(p, "SReclaimable:", 13) == 0) val = srec;
            else if (!skip) val = umin(val, L);
            if (skip) {
                fb_put(&f, p, ll);
            } else {
                char nb[24];
                size_t nl = u64_dec(nb, val);
                fb_put(&f, p, kl);
                fb_pad(&f, kl<16 ? 16-kl : 1);
                fb_pad(&f, nl<8 ? 8-nl : 0);
                fb_put(&f, nb, nl);
                fb_str(&f, " kB");
            }
        } else {
            fb_put(&f, p, ll);
        }
        fb_str(&f, "\n");
        *e = sv;
        p = sv ? e+1 : e;
    }
    return fake_end(&f);
}
struct mi {
    char maj[24], opts[256], fst[64], src[256], sup[256];
    int ok;
    size_t len;
};
static void mi_cp(char*d, size_t cap, const char*s) {
    size_t i = 0;
    while (s[i] && i+1<cap) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}
static void mi_set(struct mi*d, const char*maj, const char*o, const char*f, const char*sr, const char*su) {
    mi_cp(d->maj, sizeof d->maj, maj);
    mi_cp(d->opts, sizeof d->opts, o);
    mi_cp(d->fst, sizeof d->fst, f);
    mi_cp(d->src, sizeof d->src, sr);
    mi_cp(d->sup, sizeof d->sup, su);
    d->ok = 1;
}
static void mi_line(char*l, struct mi*r) {
    char*t[40];
    int k = 0, d = -1;
    char*p = l;
    while (*p && k<40) {
        while (*p == ' ')*p++ = 0;
        if (!*p) break;
        t[k++] = p;
        while (*p && *p != ' ') p++;
    }
    for (int i = 6; i<k; i++) if (t[i][0] == '-' && !t[i][1]) {
        d = i;
        break;
    }
    if (d<0 || d+2 >= k) return;
    const char*su = d+3<k ? t[d+3] : "";
    size_t ml = strlen(t[4]);
    if (ml && g_rl >= ml && strncmp(g_root, t[4], ml) == 0 && (ml == 1 || g_root[ml] == 0 || g_root[ml] == '/') && (!r[0].ok || ml >= r[0].len)) {
        mi_set(&r[0], t[2], t[5], t[d+1], t[d+2], su);
        r[0].len = ml;
    }
    if (strcmp(t[4], "/proc") == 0) mi_set(&r[1], t[2], t[5], t[d+1], t[d+2], su);
    if (strcmp(t[4], "/sys") == 0) mi_set(&r[2], t[2], t[5], t[d+1], t[d+2], su);
}
static void host_mounts(struct mi*r) {
    long fd = sc(N_openat, AT_FDCWD, (long) "/proc/self/mountinfo", O_RDONLY|O_CLOEXEC, 0, 0, 0);
    if (fd<0) return;
    char ck[2048], ln[PM];
    size_t ll = 0;
    long n;
    while ((n = sc(N_read, fd, (long) ck, sizeof ck, 0, 0, 0))>0) {
        for (long i = 0; i<n; i++) {
            if (ck[i] == '\n') {
                ln[ll] = 0;
                mi_line(ln, r);
                ll = 0;
            } else if (ll+1<sizeof ln) ln[ll++] = ck[i];
        }
    }
    sc(N_close, fd, 0, 0, 0, 0, 0);
}
static size_t mi_app(char*o, size_t n, size_t cap, const char*s) {
    size_t l = strlen(s);
    if (n+l+1 >= cap) return n;
    memcpy(o+n, s, l);
    return n+l;
}
static size_t build_mi(char*o, size_t cap, int info) {
    struct mi r[3];
    for (int i = 0; i<3; i++) {
        r[i].ok = 0;
        r[i].len = 0;
    }
    host_mounts(r);
    if (!r[0].ok) mi_set(&r[0], "0:1", "rw,relatime", "rootfs", "rootfs", "rw");
    if (!r[1].ok) mi_set(&r[1], "0:2", "rw,relatime", "proc", "proc", "rw");
    if (!r[2].ok) mi_set(&r[2], "0:3", "rw,relatime", "sysfs", "sysfs", "rw");
    static const char*const mp[3] = {"/", "/proc", "/sys"};
    size_t n = 0;
    for (int i = 0; i<3; i++) {
        const char*su = r[i].sup[0] ? r[i].sup : "rw";
        char id[3] = {(char)('1'+i), 0, 0};
        char par[2] = {i ? '1' : '0', 0};
        if (info) {
            n = mi_app(o, n, cap, id);
            n = mi_app(o, n, cap, " ");
            n = mi_app(o, n, cap, par);
            n = mi_app(o, n, cap, " ");
            n = mi_app(o, n, cap, r[i].maj);
            n = mi_app(o, n, cap, " / ");
            n = mi_app(o, n, cap, mp[i]);
            n = mi_app(o, n, cap, " ");
            n = mi_app(o, n, cap, r[i].opts);
            n = mi_app(o, n, cap, " - ");
            n = mi_app(o, n, cap, r[i].fst);
            n = mi_app(o, n, cap, " ");
            n = mi_app(o, n, cap, r[i].src);
            n = mi_app(o, n, cap, " ");
            n = mi_app(o, n, cap, su);
            n = mi_app(o, n, cap, "\n");
        } else {
            n = mi_app(o, n, cap, r[i].src);
            n = mi_app(o, n, cap, " ");
            n = mi_app(o, n, cap, mp[i]);
            n = mi_app(o, n, cap, " ");
            n = mi_app(o, n, cap, r[i].fst);
            n = mi_app(o, n, cap, " ");
            n = mi_app(o, n, cap, r[i].opts);
            const char*s2 = r[i].sup;
            if (s2[0] == 'r' && (s2[1] == 'w' || s2[1] == 'o') && s2[2] == ',') s2 += 3;
            if (s2[0] && strcmp(s2, "rw") && strcmp(s2, "ro")) {
                n = mi_app(o, n, cap, ",");
                n = mi_app(o, n, cap, s2);
            }
            n = mi_app(o, n, cap, " 0 0\n");
        }
    }
    return n;
}
static int do_open(int dfd, const char*p, int fl, int mode) {
    if (p) {
        if (g_memkb && strcmp(p, "/proc/meminfo") == 0) {
            if ((fl&O_ACCMODE_) != O_RDONLY) return (int) fail(EACCES);
            int mfd = fake_meminfo();
            if (mfd >= 0) return mfd;
        }
        if (g_ncpu>0) {
            if (strcmp(p, "/proc/cpuinfo") == 0) {
                if ((fl&O_ACCMODE_) != O_RDONLY) return (int) fail(EACCES);
                int mfd = fake_cpuinfo();
                if (mfd >= 0) return mfd;
            } else if (strncmp(p, "/sys/devices/system/cpu/", 24) == 0 && (strcmp(p+24, "online") == 0 || strcmp(p+24, "possible") == 0 || strcmp(p+24, "present") == 0)) {
                if ((fl&O_ACCMODE_) != O_RDONLY) return (int) fail(EACCES);
                int mfd = fake_cpurange();
                if (mfd >= 0) return mfd;
            }
        }
        if (strcmp(p, "/proc/uptime") == 0) {
            if ((fl&O_ACCMODE_) != O_RDONLY) return (int) fail(EACCES);
            const char up[] = "43200.00 42000.00\n";
            int mfd = fake_proc_file(up, sizeof (up)-1);
            if (mfd >= 0) return mfd;
        }
        if (strcmp(p, "/proc/loadavg") == 0) {
            if ((fl&O_ACCMODE_) != O_RDONLY) return (int) fail(EACCES);
            const char la[] = "0.50 0.75 0.80 1/120 12345\n";
            int mfd = fake_proc_file(la, sizeof (la)-1);
            if (mfd >= 0) return mfd;
        }
        if (strcmp(p, "/proc/version") == 0) {
            if ((fl&O_ACCMODE_) != O_RDONLY) return (int) fail(EACCES);
            const char vr[] = "Linux version 5.15.0-prx (gcc 12.2.0) #1 SMP PREEMPT\n";
            int mfd = fake_proc_file(vr, sizeof (vr)-1);
            if (mfd >= 0) return mfd;
        }
        if (strcmp(p, "/proc/filesystems") == 0) {
            if ((fl&O_ACCMODE_) != O_RDONLY) return (int) fail(EACCES);
            const char fs[] = "nodev\tsysfs\nnodev\trootfs\nnodev\tramfs\nnodev\tbdev\nnodev\tproc\nnodev\tdevpts\n\text4\n\ttmpfs\n";
            int mfd = fake_proc_file(fs, sizeof (fs)-1);
            if (mfd >= 0) return mfd;
        }
        if (strcmp(p, "/proc/stat") == 0) {
            if ((fl&O_ACCMODE_) != O_RDONLY) return (int) fail(EACCES);
            if (g_ncpu>0) {
                int sfd = fake_stat();
                if (sfd >= 0) return sfd;
            }
            const char st[] = "cpu  21200 450 12300 954000 3200 120 80 0 0 0\n""intr 1234567 10 20 0 0 0\nctxt 5678901\nbtime 1680000000\nprocesses 25000\nprocs_running 1\nprocs_blocked 0\n";
            int mfd = fake_proc_file(st, sizeof (st)-1);
            if (mfd >= 0) return mfd;
        }
        if (strcmp(p, "/proc/sys/kernel/osrelease") == 0) {
            if ((fl&O_ACCMODE_) != O_RDONLY) return (int) fail(EACCES);
            const char osr[] = "5.15.0-prx\n";
            int mfd = fake_proc_file(osr, sizeof (osr)-1);
            if (mfd >= 0) return mfd;
        }
        if (strcmp(p, "/proc/mounts") == 0 || strcmp(p, "/proc/self/mounts") == 0 || strcmp(p, "/proc/self/mountinfo") == 0 || strcmp(p, "/proc/mountinfo") == 0) {
            if ((fl&O_ACCMODE_) != O_RDONLY) return (int) fail(EACCES);
            char mb[8192];
            size_t mn = build_mi(mb, sizeof mb, p[strlen(p)-1] == 'o');
            int mfd = fake_proc_file(mb, mn);
            if (mfd >= 0) return mfd;
        }
        if (p[0] == '/' && p[1] == 'd' && p[2] == 'e' && p[3] == 'v' && p[4] == '/') {
            {
                int sn = -1;
                if (strcmp(p, "/dev/stdin") == 0) sn = 0;
                else if (strcmp(p, "/dev/stdout") == 0) sn = 1;
                else if (strcmp(p, "/dev/stderr") == 0) sn = 2;
                if (sn >= 0) return (int) ret(sc(N_fcntl, sn, (fl&O_CLOEXEC) ? 1030 : 0, 3, 0, 0, 0));
            }
            if (p[5] == 'f' && p[6] == 'd' && p[7] == '/') return (int) ret(SC4(N_openat, dfd, p, fl, mode));
        }
        if (strncmp(p, "/proc/self/fd/", 14) == 0 || strncmp(p, "/proc/thread-self/fd/", 21) == 0) return (int) ret(SC4(N_openat, dfd, p, fl, mode));
    }
    int fol = !((fl&O_NOFOLLOW) || ((fl&(O_CREAT|O_EXCL)) == (O_CREAT|O_EXCL)));
    TR1(dfd, p, fol) return (int) ret(SC4(N_openat, dfd, h1, fl|O_LARGEFILE_, mode));
}
EXP int open(const char*p, int fl, int mode) {
    return do_open(AT_FDCWD, p, fl, mode);
}
EXP int open64(const char*p, int fl, int mode) {
    return do_open(AT_FDCWD, p, fl, mode);
}
EXP int openat(int d, const char*p, int fl, int mode) {
    return do_open(d, p, fl, mode);
}
EXP int openat64(int d, const char*p, int fl, int mode) {
    return do_open(d, p, fl, mode);
}
EXP int __open_2(const char*p, int fl) {
    return do_open(AT_FDCWD, p, fl, 0);
}
EXP int __open64_2(const char*p, int fl) {
    return do_open(AT_FDCWD, p, fl, 0);
}
EXP int __openat_2(int d, const char*p, int fl) {
    return do_open(d, p, fl, 0);
}
EXP int __openat64_2(int d, const char*p, int fl) {
    return do_open(d, p, fl, 0);
}
EXP int creat(const char*p, int m) {
    return do_open(AT_FDCWD, p, O_CREAT|O_WRONLY|O_TRUNC, m);
}
EXP int creat64(const char*p, int m) {
    return do_open(AT_FDCWD, p, O_CREAT|O_WRONLY|O_TRUNC, m);
}
EXP int openat2(int d, const char*p, const void*how, size_t sz) {
    static int(*f)(int, const char*, const void*, size_t);
    TR1(d, p, 1) if (!f) f = nextsym("openat2");
    if (f) return f(d, h1, how, sz);
    return (int) ret(SC4(N_openat2, d, h1, how, sz));
}
static void*do_fopen(const char*p, const char*mode) {
    int fl = O_RDONLY, plus = 0;
    char m2[16];
    size_t k = 0;
    for (const char*c = mode; *c; c++) {
        if (*c == 'r') fl = O_RDONLY;
        else if (*c == 'w') fl = O_WRONLY|O_CREAT|O_TRUNC;
        else if (*c == 'a') fl = O_WRONLY|O_CREAT|O_APPEND;
        else if (*c == '+') plus = 1;
        else if (*c == 'x') fl |= O_EXCL;
        else if (*c == 'e') fl |= O_CLOEXEC;
        if (*c != 'x' && *c != 'e' && k<sizeof m2-1) m2[k++] = *c;
    }
    m2[k] = 0;
    if (plus) fl = (fl&~(O_WRONLY|O_RDONLY))|O_RDWR;
    int fd = do_open(AT_FDCWD, p, fl, 0666);
    if (fd<0) return NULL;
    void*f = fdopen(fd, m2);
    if (!f) sc(N_close, fd, 0, 0, 0, 0, 0);
    return f;
}
EXP void*fopen(const char*p, const char*m) {
    return do_fopen(p, m);
}
EXP void*fopen64(const char*p, const char*m) {
    return do_fopen(p, m);
}
EXP void*freopen(const char*p, const char*m, void*st) {
    static void*(*f)(const char*, const char*, void*);
    char bb[PM];
    const char*h;
    if (p && tr(AT_FDCWD, p, bb, 1, &h)<0) return NULL;
    if (!p) h = p;
    if (!f) f = nextsym("freopen");
    return f(h, m, st);
}
EXP void*freopen64(const char*p, const char*m, void*st) {
    return freopen(p, m, st);
}
EXP void*opendir(const char*p) {
    static void*(*f)(const char*);
    char b[PM];
    const char*h;
    if (tr(AT_FDCWD, p, b, 1, &h)<0) return NULL;
    if (!f) f = nextsym("opendir");
    return f(h);
}
EXP void*fdopendir(int fd) {
    static void*(*f)(int);
    if (!f) f = nextsym("fdopendir");
    return f ? f(fd) : NULL;
}
EXP void*dlopen(const char*p, int fl) {
    static void*(*f)(const char*, int);
    char b[PM];
    const char*h = p;
    if (p && p[0] == '/' && tr(AT_FDCWD, p, b, 1, &h)<0) h = p;
    if (!f) f = nextsym("dlopen");
    return f(h, fl);
}
EXP void*dlmopen(long lmid, const char*p, int fl) {
    static void*(*f)(long, const char*, int);
    char b[PM];
    const char*h = p;
    if (p && p[0] == '/' && tr(AT_FDCWD, p, b, 1, &h)<0) h = p;
    if (!f) f = nextsym("dlmopen");
    return f ? f(lmid, h, fl) : NULL;
}
#define NL2 16
static u64 g_l2d[NL2], g_l2i[NL2];
static int g_l2n;
static void l2_note(const void*b) {
    g_l2d[g_l2n%NL2] = *(const u64*) b;
    g_l2i[g_l2n%NL2] = *(const u64*)((const char*) b+OFF_INO);
    g_l2n++;
}
static int l2_has(u64 d, u64 i) {
    for (int k = 0; k<NL2 && k<g_l2n; k++) {
        if (g_l2d[k] == d && g_l2i[k] == i) return 1;
    }
    return 0;
}
static int own_under(const char*g, const char*d) {
    size_t l = strlen(d);
    return strncmp(g, d, l) == 0 && (g[l] == 0 || g[l] == '/');
}
/* user mode: files that physically belong to the host user are shown as root
   for system paths of the rootfs and as the user for home/tmp/binds. */
static int own_is_root_host(const char*h) {
    if (!h || h[0] != '/' || !hasroot(h)) return 0;
    const char*g = h+g_rl;
    if (!*g) return 1;
    if (!strncmp(g, "/home/", 6) || own_under(g, "/tmp") || own_under(g, "/var/tmp") || own_under(g, "/dev/shm") || own_under(g, "/run/user")) return 0;
    return 1;
}
static int own_is_root_fd(int fd) {
    char pp[40], hp[PM];
    if (fd == AT_FDCWD) memcpy(pp, "/proc/self/cwd", 15);
    else fdpath(pp, fd);
    long n = sc(N_readlinkat, AT_FDCWD, (long) pp, (long) hp, PM-1, 0, 0);
    if (n <= 0) return 0;
    hp[n] = 0;
    return own_is_root_host(hp);
}
static void fixstat_c(void*b, int root) {
    u32*nl = (u32*)((char*) b+OFF_NLINK);
    if (g_l2n && *nl == 1 && l2_has(*(u64*) b, *(u64*)((char*) b+OFF_INO)))*nl = 2;
    if (!g_fake && !(g_user && root)) return;
    u32*u = (u32*)((char*) b+OFF_UID);
    if (u[0] == g_ruid) u[0] = 0;
    if (u[1] == g_rgid) u[1] = 0;
}
static int do_stat(int dfd, const char*p, void*buf, int fl) {
    TR1(dfd, p, !(fl&AT_SYMLINK_NOFOLLOW)) long r = SC4(N_newfstatat, dfd, h1, buf, fl);
    if (r == 0) fixstat_c(buf, g_user ? ((p && *p && h1[0] == '/') ? own_is_root_host(h1) : own_is_root_fd(dfd)) : 1);
    return (int) ret(r);
}
static int do_fstat(int fd, void*buf) {
    long r = SC3(N_fstat, fd, buf, 0);
    if (r == 0) fixstat_c(buf, g_user ? own_is_root_fd(fd) : 1);
    return (int) ret(r);
}
#if defined(__i386__)
/* On i386 the non-LFS entry points (stat, lstat, fstat, fstatat, __xstat...)
   return glibc's 32-bit `struct stat`; the kernel only knows `struct stat64`.
   Convert, with EOVERFLOW when a value does not fit (as glibc does). */
static int stat_conv(const u8*s, u8*d) {
    u32 ino = *(const u32*)(s+12);
    u64 ino64 = *(const u64*)(s+88), size = *(const u64*)(s+44), blocks = *(const u64*)(s+56);
    if (ino64 != (u64) ino || size>0x7fffffffULL || blocks>0xffffffffULL) return (int) fail(75);
    memset(d, 0, 88);
    memcpy(d, s, 8);
    *(u32*)(d+12) = ino;
    memcpy(d+16, s+16, 16);
    memcpy(d+32, s+32, 8);
    *(u32*)(d+44) = (u32) size;
    *(u32*)(d+48) = *(const u32*)(s+52);
    *(u32*)(d+52) = (u32) blocks;
    memcpy(d+56, s+64, 24);
    return 0;
}
static int do_stat_n(int dfd, const char*p, void*buf, int fl) {
    u8 t[96];
    if (do_stat(dfd, p, t, fl)<0) return-1;
    return stat_conv(t, (u8*) buf);
}
static int do_fstat_n(int fd, void*buf) {
    u8 t[96];
    if (do_fstat(fd, t)<0) return-1;
    return stat_conv(t, (u8*) buf);
}
#else
#define do_stat_n do_stat
#define do_fstat_n do_fstat
#endif
EXP int stat(const char*p, void*b) {
    return do_stat_n(AT_FDCWD, p, b, 0);
}
EXP int stat64(const char*p, void*b) {
    return do_stat(AT_FDCWD, p, b, 0);
}
EXP int lstat(const char*p, void*b) {
    return do_stat_n(AT_FDCWD, p, b, AT_SYMLINK_NOFOLLOW);
}
EXP int lstat64(const char*p, void*b) {
    return do_stat(AT_FDCWD, p, b, AT_SYMLINK_NOFOLLOW);
}
EXP int fstatat(int d, const char*p, void*b, int f) {
    return do_stat_n(d, p, b, f);
}
EXP int fstatat64(int d, const char*p, void*b, int f) {
    return do_stat(d, p, b, f);
}
EXP int fstat(int fd, void*b) {
    return do_fstat_n(fd, b);
}
EXP int fstat64(int fd, void*b) {
    return do_fstat(fd, b);
}
EXP int __xstat(int v, const char*p, void*b) {
    (void) v;
    return do_stat_n(AT_FDCWD, p, b, 0);
}
EXP int __xstat64(int v, const char*p, void*b) {
    (void) v;
    return do_stat(AT_FDCWD, p, b, 0);
}
EXP int __lxstat(int v, const char*p, void*b) {
    (void) v;
    return do_stat_n(AT_FDCWD, p, b, AT_SYMLINK_NOFOLLOW);
}
EXP int __lxstat64(int v, const char*p, void*b) {
    (void) v;
    return do_stat(AT_FDCWD, p, b, AT_SYMLINK_NOFOLLOW);
}
EXP int __fxstat(int v, int fd, void*b) {
    (void) v;
    return do_fstat_n(fd, b);
}
EXP int __fxstat64(int v, int fd, void*b) {
    (void) v;
    return do_fstat(fd, b);
}
EXP int __fxstatat(int v, int d, const char*p, void*b, int f) {
    (void) v;
    return do_stat_n(d, p, b, f);
}
EXP int __fxstatat64(int v, int d, const char*p, void*b, int f) {
    (void) v;
    return do_stat(d, p, b, f);
}
EXP int statx(int dfd, const char*p, int fl, unsigned mask, void*buf) {
    TR1(dfd, p, !(fl&AT_SYMLINK_NOFOLLOW)) long r = SC5(N_statx, dfd, h1, fl, mask, buf);
    if (r == 0) {
        char*c = (char*) buf;
        u32*nl = (u32*)(c+16);
        if (g_l2n && *nl == 1 && (*(u32*) c&0x104) == 0x104) {
            u64 ma = *(u32*)(c+136), mi = *(u32*)(c+140);
            u64 dev = ((ma&0xfffff000ULL)<<32)|((ma&0xfffULL)<<8)|((mi&0xffffff00ULL)<<12)|(mi&0xffULL);
            if (l2_has(dev, *(u64*)(c+32)))*nl = 2;
        }
        if (g_fake || (g_user && ((p && *p && h1[0] == '/') ? own_is_root_host(h1) : own_is_root_fd(dfd)))) {
            u32*u = (u32*)(c+20);
            if (u[0] == g_ruid) u[0] = 0;
            if (u[1] == g_rgid) u[1] = 0;
        }
    }
    return (int) ret(r);
}
#if defined(__i386__)
/* Programs built with _TIME_BITS=64 call __stat64_time64 & co. and expect
   glibc's 108-byte struct stat with 64-bit timestamps, which the i386 kernel
   stat64 cannot supply. Go through statx (translated path, fake ids and
   link-count fixups are applied by the statx hook above) and repack. */
static u64 t64_dev(const u8*x) {
    u64 ma = *(const u32*) x, mi = *(const u32*)(x+4);
    return ((ma&0xfffff000ULL)<<32)|((ma&0xfffULL)<<8)|((mi&0xffffff00ULL)<<12)|(mi&0xffULL);
}
static void statx_to_t64(const u8*x, u8*d) {
    memset(d, 0, 108);
    *(u64*) d = t64_dev(x+136);
    *(u64*)(d+8) = *(const u64*)(x+32);
    *(u32*)(d+16) = *(const u16*)(x+28);
    *(u32*)(d+20) = *(const u32*)(x+16);
    *(u32*)(d+24) = *(const u32*)(x+20);
    *(u32*)(d+28) = *(const u32*)(x+24);
    *(u64*)(d+32) = t64_dev(x+128);
    *(u64*)(d+40) = *(const u64*)(x+40);
    *(u32*)(d+48) = *(const u32*)(x+4);
    *(u64*)(d+52) = *(const u64*)(x+48);
    *(u64*)(d+60) = *(const u64*)(x+64);
    *(u32*)(d+68) = *(const u32*)(x+72);
    *(u64*)(d+76) = *(const u64*)(x+112);
    *(u32*)(d+84) = *(const u32*)(x+120);
    *(u64*)(d+92) = *(const u64*)(x+96);
    *(u32*)(d+100) = *(const u32*)(x+104);
}
static int do_stat_t64(int dfd, const char*p, void*buf, int fl) {
    u8 x[256];
    if (statx(dfd, p, fl, 0x7ff, x)<0) return-1;
    statx_to_t64(x, (u8*) buf);
    return 0;
}
EXP int __stat64_time64(const char*p, void*b) {
    return do_stat_t64(AT_FDCWD, p, b, 0);
}
EXP int __lstat64_time64(const char*p, void*b) {
    return do_stat_t64(AT_FDCWD, p, b, AT_SYMLINK_NOFOLLOW);
}
EXP int __fstat64_time64(int fd, void*b) {
    return do_stat_t64(fd, "", b, 0x1000);
}
EXP int __fstatat64_time64(int d, const char*p, void*b, int f) {
    return do_stat_t64(d, p, b, f);
}
#endif
static int do_access(int dfd, const char*p, int mode, int fl) {
    TR1(dfd, p, !(fl&AT_SYMLINK_NOFOLLOW)) long r = SC3(N_faccessat, dfd, h1, mode);
    if (r == -EACCES && VROOT()) {
        char st[256];
        if (SC4(N_newfstatat, dfd, h1, st, fl&AT_SYMLINK_NOFOLLOW) == 0) {
            u32 fmode = *(u32*)(st+MODE_OFF);
            if ((mode&X_OK) && !(fmode&0111)) return (int) fail(EACCES);
            return 0;
        }
    }
    return (int) ret(r);
}
EXP int access(const char*p, int m) {
    return do_access(AT_FDCWD, p, m, 0);
}
EXP int faccessat(int d, const char*p, int m, int f) {
    return do_access(d, p, m, f);
}
EXP int faccessat2(int d, const char*p, int m, int f) {
    return do_access(d, p, m, f);
}
EXP int eaccess(const char*p, int m) {
    return do_access(AT_FDCWD, p, m, 0);
}
EXP int euidaccess(const char*p, int m) {
    return do_access(AT_FDCWD, p, m, 0);
}
static long do_readlink(int dfd, const char*p, char*buf, size_t sz) {
    if (g_rl && p && g_self_exe[0] && is_proc_exe(p)) {
        size_t el = strlen(g_self_exe);
        size_t n = el<sz ? el : sz;
        memcpy(buf, g_self_exe, n);
        return (long) n;
    }
    TR1(dfd, p, 0) long r = sc(N_readlinkat, dfd, (long) h1, (long) buf, (long) sz, 0, 0);
    if (r>0 && g_rl && (size_t) r >= g_rl && !memcmp(buf, g_root, g_rl) && ((size_t) r == g_rl || buf[g_rl] == '/')) {
        size_t nl = (size_t) r-g_rl;
        if (!nl) {
            buf[0] = '/';
            r = 1;
        } else {
            memmove(buf, buf+g_rl, nl);
            r = (long) nl;
        }
    }
    return ret(r);
}
EXP long readlink(const char*p, char*b, size_t n) {
    return do_readlink(AT_FDCWD, p, b, n);
}
EXP long readlinkat(int d, const char*p, char*b, size_t n) {
    return do_readlink(d, p, b, n);
}
EXP long __readlink_chk(const char*p, char*b, size_t n, size_t bl) {
    (void) bl;
    return do_readlink(AT_FDCWD, p, b, n);
}
EXP long __readlinkat_chk(int d, const char*p, char*b, size_t n, size_t bl) {
    (void) bl;
    return do_readlink(d, p, b, n);
}
EXP int chdir(const char*p) {
    TR1(AT_FDCWD, p, 1) return (int) ret(SC3(N_chdir, h1, 0, 0));
}
EXP int fchdir(int fd) {
    static int(*f)(int);
    if (!f) f = nextsym("fchdir");
    return f ? f(fd) : (int) ret(sc(N_fchdir, fd, 0, 0, 0, 0, 0));
}
static char*do_getcwd(char*buf, size_t size) {
    char tmp[PM];
    long n = sc(N_getcwd, (long) tmp, PM, 0, 0, 0, 0);
    if (n<0) {
        fail((int)-n);
        return NULL;
    }
    const char*s = tmp;
    char rb[PM];
    if (hasroot(tmp)) {
        s = tmp+g_rl;
        if (!*s) s = "/";
    } else if (g_rl && rev_bind(tmp, rb)) {
        s = rb;
    }
    size_t l = strlen(s)+1;
    if (!buf) {
        size_t cap = size ? size : l;
        if (cap<l) {
            fail(ERANGE);
            return NULL;
        }
        buf = malloc(cap);
        if (!buf) {
            fail(ENOMEM);
            return NULL;
        }
    } else if (size<l) {
        fail(ERANGE);
        return NULL;
    }
    memcpy(buf, s, l);
    return buf;
}
EXP char*getcwd(char*b, size_t n) {
    return do_getcwd(b, n);
}
EXP char*__getcwd_chk(char*b, size_t n, size_t bl) {
    (void) bl;
    return do_getcwd(b, n);
}
static char*do_realpath(const char*p, char*out) {
    static char*(*real)(const char*, char*);
    if (!p) {
        fail(EINVAL);
        return NULL;
    }
    char g[PM], res[PM], hb[PM];
    if (g_rl && g_self_exe[0] && is_proc_exe(p)) p = g_self_exe;
    int m = g_rl ? mkabs(AT_FDCWD, p, g) : 1;
    if (m<0) return NULL;
    if (m>0) {
        if (!real) real = nextsym("realpath");
        return real(p, out);
    }
    int bind;
    if (canon(g, res, 1, &bind)<0) return NULL;
    const char*h = res;
    if (bind) {
        if (bind_to_host(bind-1, res, hb)<0) return NULL;
        h = hb;
    } else {
        size_t rl = strlen(res);
        memcpy(hb, g_root, g_rl);
        if (rl>1) memcpy(hb+g_rl, res, rl+1);
        else hb[g_rl] = 0;
        h = hb;
    }
    char st[256];
    long r = SC4(N_newfstatat, AT_FDCWD, h, st, 0);
    if (r<0) {
        fail((int)-r);
        return NULL;
    }
    size_t l = strlen(res)+1;
    if (!out) {
        out = malloc(l>PM ? l : PM);
        if (!out) {
            fail(ENOMEM);
            return NULL;
        }
    }
    memcpy(out, res, l);
    return out;
}
EXP char*realpath(const char*p, char*o) {
    return do_realpath(p, o);
}
EXP char*__realpath_chk(const char*p, char*o, size_t ol) {
    (void) ol;
    return do_realpath(p, o);
}
EXP char*canonicalize_file_name(const char*p) {
    return do_realpath(p, NULL);
}
EXP char*get_current_dir_name(void) {
    return do_getcwd(NULL, 0);
}
EXP int mkdir(const char*p, int m) {
    TR1(AT_FDCWD, p, 0) return (int) ret(SC3(N_mkdirat, AT_FDCWD, h1, m));
}
EXP int mkdirat(int d, const char*p, int m) {
    TR1(d, p, 0) return (int) ret(SC3(N_mkdirat, d, h1, m));
}
EXP int rmdir(const char*p) {
    TR1(AT_FDCWD, p, 0) return (int) ret(SC3(N_unlinkat, AT_FDCWD, h1, AT_REMOVEDIR));
}
EXP int unlink(const char*p) {
    TR1(AT_FDCWD, p, 0) return (int) ret(SC3(N_unlinkat, AT_FDCWD, h1, 0));
}
EXP int unlinkat(int d, const char*p, int f) {
    TR1(d, p, 0) return (int) ret(SC3(N_unlinkat, d, h1, f));
}
EXP int remove(const char*p) {
    TR1(AT_FDCWD, p, 0) long r = SC3(N_unlinkat, AT_FDCWD, h1, 0);
    if (r == -21 || r == -1) r = SC3(N_unlinkat, AT_FDCWD, h1, AT_REMOVEDIR);
    return (int) ret(r);
}
static int do_rename(int od, const char*o, int nd, const char*n, unsigned fl) {
    char b1[PM], b2[PM];
    const char*h1, *h2;
    if (tr(od, o, b1, 0, &h1)<0 || tr(nd, n, b2, 0, &h2)<0) return-1;
    if (fl) return (int) ret(SC5(N_renameat2, od, h1, nd, h2, fl));
    return (int) ret(SC4(N_renameat, od, h1, nd, h2));
}
EXP int rename(const char*o, const char*n) {
    return do_rename(AT_FDCWD, o, AT_FDCWD, n, 0);
}
EXP int renameat(int od, const char*o, int nd, const char*n) {
    return do_rename(od, o, nd, n, 0);
}
EXP int renameat2(int od, const char*o, int nd, const char*n, unsigned f) {
    return do_rename(od, o, nd, n, f);
}
static long link_fallback(int od, const char*src, int nd, const char*dst, int follow) {
    char st[256];
    long r = SC4(N_newfstatat, od, src, st, follow ? 0 : AT_SYMLINK_NOFOLLOW);
    if (r<0) return r;
    u32 mode = *(u32*)(st+MODE_OFF);
    if ((mode&0170000) == 0120000) {
        char t[PM];
        long n = sc(N_readlinkat, od, (long) src, (long) t, PM-1, 0, 0);
        if (n<0) return n;
        t[n] = 0;
        return SC3(N_symlinkat, t, nd, dst);
    }
    if ((mode&0170000) != 0100000) return-EPERM;
    long in = SC4(N_openat, od, src, O_RDONLY|O_CLOEXEC|O_LARGEFILE_|(follow ? 0 : O_NOFOLLOW), 0);
    if (in<0) return in;
    long out = SC4(N_openat, nd, dst, O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_LARGEFILE_, mode&07777);
    if (out<0) {
        sc(N_close, in, 0, 0, 0, 0, 0);
        return out;
    }
    char buf[32768];
    long err = 0;
    for (;; ) {
        long n = SC3(N_read, in, buf, sizeof buf);
        if (n<0) {
            err = n;
            break;
        }
        if (n == 0) break;
        long off = 0;
        while (off<n) {
            long w = SC3(N_write, out, buf+off, n-off);
            if (w<0) {
                err = w;
                goto copy_done;
            }
            off += w;
        }
    }
    copy_done : if (!err) {
        sc(N_fchmod, out, (VROOT() ? (mode|0600) : mode)&07777, 0, 0, 0, 0);
        l2_note(st);
    }
    sc(N_close, in, 0, 0, 0, 0, 0);
    sc(N_close, out, 0, 0, 0, 0, 0);
    if (err) SC3(N_unlinkat, nd, dst, 0);
    return err;
}
static int do_link(int od, const char*o, int nd, const char*n, int fl) {
    char b1[PM], b2[PM];
    const char*h1, *h2;
    int fol = !!(fl&AT_SYMLINK_FOLLOW);
    if (tr(od, o, b1, fol, &h1)<0 || tr(nd, n, b2, 0, &h2)<0) return-1;
    long r = SC5(N_linkat, od, h1, nd, h2, fl);
    if (r == -EACCES || r == -EPERM || r == -18) {
        long r2 = link_fallback(od, h1, nd, h2, fol);
        if (r2 == 0) return 0;
        if (r2 != -EPERM && r2 != -EACCES) r = r2;
    }
    return (int) ret(r);
}
EXP int link(const char*o, const char*n) {
    return do_link(AT_FDCWD, o, AT_FDCWD, n, 0);
}
EXP int linkat(int od, const char*o, int nd, const char*n, int f) {
    return do_link(od, o, nd, n, f);
}
EXP int symlink(const char*target, const char*p) {
    TR1(AT_FDCWD, p, 0) return (int) ret(SC3(N_symlinkat, target, AT_FDCWD, h1));
}
EXP int symlinkat(const char*target, int d, const char*p) {
    TR1(d, p, 0) return (int) ret(SC3(N_symlinkat, target, d, h1));
}
static int fake_mode(const char*st, int m) {
    if (!VROOT()) return m;
    u32 fmode = *(const u32*)(st+MODE_OFF);
    if ((fmode&0170000) == 0040000) return m|0700;
    return m|0600;
}
EXP int chmod(const char*p, int m) {
    TR1(AT_FDCWD, p, 1) if (VROOT()) {
        char st[256];
        if (SC4(N_newfstatat, AT_FDCWD, h1, st, 0) == 0) m = fake_mode(st, m);
    }
    return (int) ret(SC3(N_fchmodat, AT_FDCWD, h1, m));
}
EXP int fchmod(int fd, int m) {
    if (VROOT()) {
        char st[256];
        if (sc(N_fstat, fd, (long) st, 0, 0, 0, 0) == 0) m = fake_mode(st, m);
    }
    return (int) ret(sc(N_fchmod, fd, m, 0, 0, 0, 0));
}
EXP int fchmodat(int d, const char*p, int m, int f) {
    TR1(d, p, !(f&AT_SYMLINK_NOFOLLOW)) char st[256];
    if (f&AT_SYMLINK_NOFOLLOW) {
        long r = SC4(N_newfstatat, d, h1, st, AT_SYMLINK_NOFOLLOW);
        if (r<0) {
            if (VROOT()) return 0;
            return (int) ret(r);
        }
        u32 mode = *(u32*)(st+MODE_OFF);
        if ((mode&0170000) == 0120000) return (int) fail(EOPNOTSUPP);
        m = fake_mode(st, m);
    } else if (VROOT() && SC4(N_newfstatat, d, h1, st, 0) == 0) {
        m = fake_mode(st, m);
    }
    long res = SC3(N_fchmodat, d, h1, m);
    if (res<0 && VROOT()) return 0;
    return (int) ret(res);
}
static int do_chown(int d, const char*p, u32 u, u32 g, int fl) {
    TR1(d, p, !(fl&AT_SYMLINK_NOFOLLOW)) if (VROOT()) {
        char cst[256];
        long cr = SC4(N_newfstatat, d, h1, cst, fl&AT_SYMLINK_NOFOLLOW);
        return cr<0 ? (int) ret(cr) : 0;
    }
    return (int) ret(SC5(N_fchownat, d, h1, u, g, fl));
}
EXP int chown(const char*p, u32 u, u32 g) {
    return do_chown(AT_FDCWD, p, u, g, 0);
}
EXP int lchown(const char*p, u32 u, u32 g) {
    return do_chown(AT_FDCWD, p, u, g, AT_SYMLINK_NOFOLLOW);
}
EXP int fchownat(int d, const char*p, u32 u, u32 g, int f) {
    return do_chown(d, p, u, g, f);
}
EXP int fchown(int fd, u32 u, u32 g) {
    static int(*f)(int, u32, u32);
    if (VROOT()) return 0;
    if (!f) f = nextsym("fchown");
    return f(fd, u, g);
}
EXP int truncate(const char*p, long len) {
    TR1(AT_FDCWD, p, 1) return (int) ret(SC3(N_truncate, h1, len, 0));
}
EXP int truncate64(const char*p, s64 len) {
#if defined(__i386__)
    TR1(AT_FDCWD, p, 1) return (int) ret(sc(N_truncate64, (long) h1, (long)(u32) len, (long)(u32)((u64) len>>32), 0, 0, 0));
#else
    TR1(AT_FDCWD, p, 1) return (int) ret(SC3(N_truncate, h1, len, 0));
#endif
}
EXP int ftruncate(int fd, long len) {
    static int(*f)(int, long);
    if (!f) f = nextsym("ftruncate");
    return f ? f(fd, len) : 0;
}
EXP int ftruncate64(int fd, s64 len) {
    static int(*f)(int, s64);
    if (!f) f = nextsym("ftruncate64");
    return f ? f(fd, len) : 0;
}
EXP int utimensat(int d, const char*p, const void*t, int f) {
    if (!p) return (int) ret(SC4(N_utimensat, d, 0, t, f));
    TR1(d, p, !(f&AT_SYMLINK_NOFOLLOW)) return (int) ret(SC4(N_utimensat, d, h1, t, f));
}
#if defined(__i386__)
EXP int __utimensat64(int d, const char*p, const void*t, int f) {
    if (!p) return (int) ret(SC4(N_utimensat_time64, d, 0, t, f));
    TR1(d, p, !(f&AT_SYMLINK_NOFOLLOW)) return (int) ret(SC4(N_utimensat_time64, d, h1, t, f));
}
#endif
static int do_mknod(int d, const char*p, int m, unsigned long dev) {
    TR1(d, p, 0) long r = SC4(N_mknodat, d, h1, m, dev);
    if (r == -EPERM && VROOT() && ((m&0170000) == 0020000 || (m&0170000) == 0060000)) {
        long fd = SC4(N_openat, d, h1, O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC, 0600);
        if (fd >= 0) {
            sc(N_close, fd, 0, 0, 0, 0, 0);
            return 0;
        }
        if (fd == -EEXIST) return 0;
    }
    return (int) ret(r);
}
EXP int mknod(const char*p, int m, unsigned long dev) {
    return do_mknod(AT_FDCWD, p, m, dev);
}
EXP int mknodat(int d, const char*p, int m, unsigned long dev) {
    return do_mknod(d, p, m, dev);
}
EXP int mkfifo(const char*p, int m) {
    TR1(AT_FDCWD, p, 0) return (int) ret(SC4(N_mknodat, AT_FDCWD, h1, (m&07777)|0010000, 0));
}
EXP int mkfifoat(int d, const char*p, int m) {
    TR1(d, p, 0) return (int) ret(SC4(N_mknodat, d, h1, (m&07777)|0010000, 0));
}
EXP int chroot(const char*path) {
    (void) path;
    if (VROOT()) return 0;
    static int(*f)(const char*);
    if (!f) f = nextsym("chroot");
    return f ? f(path) : 0;
}
EXP int pivot_root(const char*new_root, const char*put_old) {
    (void) new_root;
    (void) put_old;
    if (VROOT()) return 0;
    return (int) fail(EPERM);
}
EXP int mount(const char*s, const char*t, const char*fs, unsigned long flags, const void*data) {
    (void) s;
    (void) t;
    (void) fs;
    (void) flags;
    (void) data;
    if (VROOT()) return 0;
    return (int) fail(EPERM);
}
EXP int umount(const char*t) {
    (void) t;
    if (VROOT()) return 0;
    return (int) fail(EPERM);
}
EXP int umount2(const char*t, int flags) {
    (void) t;
    (void) flags;
    if (VROOT()) return 0;
    return (int) fail(EPERM);
}
EXP int swapon(const char*path, int swapflags) {
    (void) path;
    (void) swapflags;
    if (VROOT()) return 0;
    return (int) fail(EPERM);
}
EXP int swapoff(const char*path) {
    (void) path;
    if (VROOT()) return 0;
    return (int) fail(EPERM);
}
#define XA_GET(name,nr,fol) EXP long name(const char*p,const char*n,void*v,size_t s){TR1(AT_FDCWD,p,fol)return ret(sc(nr,(long)h1,(long)n,(long)v,(long)s,0,0));}
XA_GET(getxattr, N_getxattr, 1) XA_GET(lgetxattr, N_lgetxattr, 0)
#define XA_LIST(name,nr,fol) EXP long name(const char*p,char*l,size_t s){TR1(AT_FDCWD,p,fol)return ret(SC3(nr,h1,l,s));}
XA_LIST(listxattr, N_listxattr, 1) XA_LIST(llistxattr, N_llistxattr, 0)
#define XA_SET(name,nr,fol) EXP int name(const char*p,const char*n,const void*v,size_t s,int f){TR1(AT_FDCWD,p,fol)return(int)ret(sc(nr,(long)h1,(long)n,(long)v,(long)s,f,0));}
XA_SET(setxattr, N_setxattr, 1) XA_SET(lsetxattr, N_lsetxattr, 0)
#define XA_REM(name,nr,fol) EXP int name(const char*p,const char*n){TR1(AT_FDCWD,p,fol)return(int)ret(SC3(nr,h1,n,0));}
XA_REM(removexattr, N_removexattr, 1) XA_REM(lremovexattr, N_lremovexattr, 0) EXP long fgetxattr(int fd, const char*n, void*v, size_t s) {
    static long(*f)(int, const char*, void*, size_t);
    if (!f) f = nextsym("fgetxattr");
    return f ? f(fd, n, v, s) : -1;
}
EXP int fsetxattr(int fd, const char*n, const void*v, size_t s, int flags) {
    static int(*f)(int, const char*, const void*, size_t, int);
    if (!f) f = nextsym("fsetxattr");
    return f ? f(fd, n, v, s, flags) : 0;
}
EXP long flistxattr(int fd, char*l, size_t s) {
    static long(*f)(int, char*, size_t);
    if (!f) f = nextsym("flistxattr");
    return f ? f(fd, l, s) : -1;
}
EXP int fremovexattr(int fd, const char*n) {
    static int(*f)(int, const char*);
    if (!f) f = nextsym("fremovexattr");
    return f ? f(fd, n) : 0;
}
static void fix_statfs(void*b) {
    if (!b) return;
    unsigned long t = *(unsigned long*) b;
    if (t == 0x9fa0UL || t == 0x62656572UL) return;
    *(unsigned long*) b = 0xef53UL;
}
EXP int statfs(const char*p, void*b) {
    TR1(AT_FDCWD, p, 1) long r = SC3(N_statfs, h1, b, 0);
    if (r == 0) fix_statfs(b);
    return (int) ret(r);
}
#if defined(__i386__)
#define SC_STATFS64(h,b) sc(N_statfs64, (long)(h), 84, (long)(b), 0, 0, 0)
#define SC_FSTATFS64(fd,b) sc(N_fstatfs64, (long)(fd), 84, (long)(b), 0, 0, 0)
#else
#define SC_STATFS64(h,b) SC3(N_statfs, h, b, 0)
#define SC_FSTATFS64(fd,b) SC3(N_fstatfs, fd, b, 0)
#endif
EXP int statfs64(const char*p, void*b) {
    TR1(AT_FDCWD, p, 1) long r = SC_STATFS64(h1, b);
    if (r == 0) fix_statfs(b);
    return (int) ret(r);
}
EXP int fstatfs(int fd, void*b) {
    long r = SC3(N_fstatfs, fd, b, 0);
    if (r == 0) fix_statfs(b);
    return (int) ret(r);
}
EXP int fstatfs64(int fd, void*b) {
    long r = SC_FSTATFS64(fd, b);
    if (r == 0) fix_statfs(b);
    return (int) ret(r);
}
EXP int statvfs(const char*p, void*b) {
    static int(*f)(const char*, void*);
    char bb[PM];
    const char*h;
    if (tr(AT_FDCWD, p, bb, 1, &h)<0) return-1;
    if (!f) f = nextsym("statvfs");
    return f ? f(h, b) : -1;
}
EXP int statvfs64(const char*p, void*b) {
    static int(*f)(const char*, void*);
    char bb[PM];
    const char*h;
    if (tr(AT_FDCWD, p, bb, 1, &h)<0) return-1;
    if (!f) f = nextsym("statvfs64");
    return f ? f(h, b) : -1;
}
EXP int fstatvfs(int fd, void*b) {
    static int(*f)(int, void*);
    if (!f) f = nextsym("fstatvfs");
    return f ? f(fd, b) : -1;
}
EXP int fstatvfs64(int fd, void*b) {
    static int(*f)(int, void*);
    if (!f) f = nextsym("fstatvfs64");
    return f ? f(fd, b) : -1;
}
#define TRWRAP(rt,name,fol,T2) EXP rt name(const char*p,T2 a){static rt(*f)(const char*,T2);char bb[PM];const char*h;if(tr(AT_FDCWD,p,bb,(fol),&h)<0)return(rt)-1;if(!f)f=nextsym(#name);return f(h,a);}
TRWRAP(int, utimes, 1, const void*) TRWRAP(int, utime, 1, const void*) TRWRAP(int, lutimes, 0, const void*) TRWRAP(long, pathconf, 1, int) EXP int futimesat(int d, const char*p, const void*tv) {
    static int(*f)(int, const char*, const void*);
    char bb[PM];
    const char*h;
    if (p && tr(d, p, bb, 1, &h)<0) return-1;
    if (!p) h = p;
    if (!f) f = nextsym("futimesat");
    return f(d, h, tv);
}
EXP int futimes(int fd, const void*tv) {
    static int(*f)(int, const void*);
    if (!f) f = nextsym("futimes");
    return f ? f(fd, tv) : 0;
}
EXP long fpathconf(int fd, int name) {
    static long(*f)(int, int);
    if (!f) f = nextsym("fpathconf");
    return f ? f(fd, name) : -1;
}
EXP int ioctl(int fd, unsigned long req, void*arg) {
    static int(*f)(int, unsigned long, void*);
    if (!f) f = nextsym("ioctl");
    return f ? f(fd, req, arg) : (int) ret(sc(N_ioctl, fd, (long) req, (long) arg, 0, 0, 0));
}
/* Fallback tty untuk glibc baru: Android/SELinux menolak TCGETS2 (EACCES) */
EXP int isatty(int fd) {
    static int(*f)(int);
    u8 k[64];
    if (!f) f = nextsym("isatty");
    if (f && f(fd)) return 1;
    long r = sc(N_ioctl, fd, 0x5401, (long) k, 0, 0, 0);
    if (r < 0) {
        *__errno_location() = (int) -r;
        return 0;
    }
    return 1;
}
EXP int tcgetattr(int fd, void*t) {
    static int(*f)(int, void*);
    u8 k[64];
    if (!f) f = nextsym("tcgetattr");
    if (f && f(fd, t) == 0) return 0;
    long r = sc(N_ioctl, fd, 0x5401, (long) k, 0, 0, 0);
    if (r < 0) {
        *__errno_location() = (int) -r;
        return -1;
    }
    u8*d = (u8*) t;
    memcpy(d, k, 16);
    d[16] = k[16];
    memcpy(d+17, k+17, 19);
    memset(d+36, 0, 16);
    ((u32*) t)[13] = ((u32*) t)[14] = ((u32*) t)[2] & 0x100f;
    return 0;
}
EXP int tcsetattr(int fd, int act, const void*t) {
    static int(*f)(int, int, const void*);
    u8 k[64];
    if (!f) f = nextsym("tcsetattr");
    if (f && f(fd, act, t) == 0) return 0;
    if (act < 0 || act > 2) {
        *__errno_location() = EINVAL;
        return -1;
    }
    memset(k, 0, sizeof k);
    memcpy(k, t, 16);
    k[16] = ((const u8*) t)[16];
    memcpy(k+17, (const u8*) t+17, 19);
    long r = sc(N_ioctl, fd, 0x5402+act, (long) k, 0, 0, 0);
    if (r < 0) {
        *__errno_location() = (int) -r;
        return -1;
    }
    return 0;
}
EXP int fcntl(int fd, int cmd, void*arg) {
    static int(*f)(int, int, void*);
    if (!f) f = nextsym("fcntl");
    return f ? f(fd, cmd, arg) : (int) ret(sc(N_fcntl, fd, cmd, (long) arg, 0, 0, 0));
}
EXP int fcntl64(int fd, int cmd, void*arg) {
    static int(*f)(int, int, void*);
    if (!f) f = nextsym("fcntl64");
    if (!f) f = nextsym("fcntl");
    return f ? f(fd, cmd, arg) : (int) ret(sc(N_fcntl, fd, cmd, (long) arg, 0, 0, 0));
}
typedef struct {
    size_t gl_pathc;
    char**gl_pathv;
    size_t gl_offs;
    int gl_flags;
} prx_glob_t;
static void glob_strip(prx_glob_t*pg, int flags) {
    if (!g_rl || !pg || !pg->gl_pathv) return;
    size_t start = (flags&0x08) ? pg->gl_offs : 0;
    for (size_t i = 0; i<pg->gl_pathc; i++) {
        char*p = pg->gl_pathv[start+i];
        if (p && hasroot(p)) {
            size_t l = strlen(p+g_rl);
            if (l == 0) {
                p[0] = '/';
                p[1] = 0;
            } else {
                memmove(p, p+g_rl, l+1);
            }
        }
    }
}
EXP int glob(const char*pat, int fl, int(*err)(const char*, int), void*pg) {
    static int(*f)(const char*, int, int(*)(const char*, int), void*);
    char b[PM];
    const char*h;
    if (!f) f = nextsym("glob");
    if (!f) return (int) fail(ENOSYS);
    if (pat && pat[0] != '/') return f(pat, fl, err, pg);
    if (tr(AT_FDCWD, pat, b, 0, &h)<0) return-1;
    int r = f(h, fl, err, pg);
    if (r == 0) glob_strip((prx_glob_t*) pg, fl);
    return r;
}
EXP int glob64(const char*pat, int fl, int(*err)(const char*, int), void*pg) {
    static int(*f)(const char*, int, int(*)(const char*, int), void*);
    char b[PM];
    const char*h;
    if (!f) f = nextsym("glob64");
    if (!f) f = nextsym("glob");
    if (!f) return (int) fail(ENOSYS);
    if (pat && pat[0] != '/') return f(pat, fl, err, pg);
    if (tr(AT_FDCWD, pat, b, 0, &h)<0) return-1;
    int r = f(h, fl, err, pg);
    if (r == 0) glob_strip((prx_glob_t*) pg, fl);
    return r;
}
EXP int scandir(const char*p, void*nl, void*flt, void*cmp) {
    static int(*f)(const char*, void*, void*, void*);
    char bb[PM];
    const char*h;
    if (tr(AT_FDCWD, p, bb, 1, &h)<0) return-1;
    if (!f) f = nextsym("scandir");
    return f(h, nl, flt, cmp);
}
EXP int scandir64(const char*p, void*nl, void*flt, void*cmp) {
    static int(*f)(const char*, void*, void*, void*);
    char bb[PM];
    const char*h;
    if (tr(AT_FDCWD, p, bb, 1, &h)<0) return-1;
    if (!f) f = nextsym("scandir64");
    return f(h, nl, flt, cmp);
}
static int tmpgen(char*t, int suffix, int flags, int isdir) {
    size_t n = strlen(t);
    if ((size_t) suffix>n || n-(size_t) suffix<6 || memcmp(t+n-suffix-6, "XXXXXX", 6) != 0) return (int) fail(EINVAL);
    char b[PM];
    const char*h;
    if (tr(AT_FDCWD, t, b, 0, &h)<0) return-1;
    char hb[PM];
    size_t hl = strlen(h);
    if (hl >= PM) return (int) fail(ENAMETOOLONG);
    memcpy(hb, h, hl+1);
    static const char al[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int tries = 0; tries<200; tries++) {
        u8 rnd[6];
        sc(N_getrandom, (long) rnd, 6, 0, 0, 0, 0);
        for (int i = 0; i<6; i++) {
            char c = al[rnd[i]%62];
            t[n-suffix-6+i] = c;
            hb[hl-suffix-6+i] = c;
        }
        long r;
        if (isdir) r = SC3(N_mkdirat, AT_FDCWD, hb, 0700);
        else r = SC4(N_openat, AT_FDCWD, hb, O_RDWR|O_CREAT|O_EXCL|O_LARGEFILE_|(flags&~O_ACCMODE_), 0600);
        if (r >= 0) return isdir ? 0 : (int) r;
        if (r != -EEXIST) return (int) ret(r);
    }
    return (int) fail(EEXIST);
}
EXP int mkstemp(char*t) {
    return tmpgen(t, 0, 0, 0);
}
EXP int mkstemp64(char*t) {
    return tmpgen(t, 0, 0, 0);
}
EXP int mkostemp(char*t, int f) {
    return tmpgen(t, 0, f, 0);
}
EXP int mkostemp64(char*t, int f) {
    return tmpgen(t, 0, f, 0);
}
EXP int mkstemps(char*t, int s) {
    return tmpgen(t, s, 0, 0);
}
EXP int mkstemps64(char*t, int s) {
    return tmpgen(t, s, 0, 0);
}
EXP char*mkdtemp(char*t) {
    return tmpgen(t, 0, 0, 1)<0 ? NULL : t;
}
#define O_PATH_ 0x200000
static int un_fix(const void*a, unsigned len, int follow, u8*na, unsigned*nl, int*tfd) {
    *tfd = -1;
    if (!g_rl || !a || len<3 || *(const u16*) a != 1) return 0;
    const char*sp = (const char*) a+2;
    if (sp[0] == 0) return 0;
    char p[PM], b[PM];
    size_t n = len-2, k = 0;
    if (n>108) n = 108;
    while (k<n && sp[k]) k++;
    memcpy(p, sp, k);
    p[k] = 0;
    const char*h;
    if (tr(AT_FDCWD, p, b, follow, &h)<0) return-1;
    if (h == p) return 0;
    size_t hl = strlen(h);
    memset(na, 0, 110);
    *(u16*) na = 1;
    if (hl<108) {
        memcpy(na+2, h, hl+1);
        *nl = (unsigned)(2+hl+1);
        return 1;
    }
    size_t sl = hl;
    while (sl>0 && h[sl-1] != '/') sl--;
    char dir[PM];
    if (sl <= 1) {
        dir[0] = '/';
        dir[1] = 0;
    } else {
        memcpy(dir, h, sl-1);
        dir[sl-1] = 0;
    }
    long fd = SC4(N_openat, AT_FDCWD, dir, O_PATH_|O_CLOEXEC, 0);
    if (fd<0) {
        fail((int)-fd);
        return-1;
    }
    char fp[48];
    fdpath(fp, (int) fd);
    size_t fl = strlen(fp), bl = strlen(h+sl);
    if (fl+1+bl >= 108) {
        sc(N_close, fd, 0, 0, 0, 0, 0);
        fail(ENAMETOOLONG);
        return-1;
    }
    memcpy(na+2, fp, fl);
    na[2+fl] = '/';
    memcpy(na+3+fl, h+sl, bl+1);
    *nl = (unsigned)(2+fl+1+bl+1);
    *tfd = (int) fd;
    return 1;
}
static void un_done(int tfd) {
    if (tfd >= 0) sc(N_close, tfd, 0, 0, 0, 0, 0);
}
static void un_strip(void*a, unsigned*len) {
    if (!g_rl || !a || !len || *len<3 || *(u16*) a != 1) return;
    char*sp = (char*) a+2;
    size_t n = *len-2, k = 0;
    if (n>108) n = 108;
    while (k<n && sp[k]) k++;
    if (!k || k >= 108) return;
    char t[110], rb[PM];
    memcpy(t, sp, k);
    t[k] = 0;
    const char*g = NULL;
    if (hasroot(t)) {
        g = t+g_rl;
        if (!*g) g = "/";
    } else {
        g = rev_bind(t, rb);
    }
    if (!g) return;
    size_t gl = strlen(g);
    if (gl >= 108) return;
    memset(sp, 0, n);
    memcpy(sp, g, gl+1);
    *len = (unsigned)(2+gl+1);
}
EXP int connect(int fd, const void*a, unsigned l) {
    static int(*f)(int, const void*, unsigned);
    u8 na[110];
    unsigned nl = 0;
    int tfd, r = un_fix(a, l, 1, na, &nl, &tfd);
    if (r<0) return-1;
    if (!f) f = nextsym("connect");
    int rv = r ? f(fd, na, nl) : f(fd, a, l);
    int e = geterr();
    un_done(tfd);
    *__errno_location() = e;
    return rv;
}
EXP int bind(int fd, const void*a, unsigned l) {
    static int(*f)(int, const void*, unsigned);
    u8 na[110];
    unsigned nl = 0;
    int tfd, r = un_fix(a, l, 0, na, &nl, &tfd);
    if (r<0) return-1;
    if (!f) f = nextsym("bind");
    int rv = r ? f(fd, na, nl) : f(fd, a, l);
    int e = geterr();
    un_done(tfd);
    *__errno_location() = e;
    return rv;
}
EXP long sendto(int fd, const void*buf, size_t len, int fl, const void*a, unsigned al) {
    static long(*f)(int, const void*, size_t, int, const void*, unsigned);
    u8 na[110];
    unsigned nl = 0;
    int tfd, r = a ? un_fix(a, al, 1, na, &nl, &tfd) : (tfd = -1, 0);
    if (r<0) return-1;
    if (!f) f = nextsym("sendto");
    long rv = r ? f(fd, buf, len, fl, na, nl) : f(fd, buf, len, fl, a, al);
    int e = geterr();
    un_done(tfd);
    *__errno_location() = e;
    return rv;
}
EXP long recvfrom(int fd, void*buf, size_t len, int fl, void*a, unsigned*al) {
    static long(*f)(int, void*, size_t, int, void*, unsigned*);
    if (!f) f = nextsym("recvfrom");
    long r = f ? f(fd, buf, len, fl, a, al) : -1;
    if (r >= 0 && a && al) un_strip(a, al);
    return r;
}
EXP int getsockname(int fd, void*a, unsigned*l) {
    static int(*f)(int, void*, unsigned*);
    if (!f) f = nextsym("getsockname");
    int r = f(fd, a, l);
    if (r == 0) un_strip(a, l);
    return r;
}
EXP int getpeername(int fd, void*a, unsigned*l) {
    static int(*f)(int, void*, unsigned*);
    if (!f) f = nextsym("getpeername");
    int r = f(fd, a, l);
    if (r == 0) un_strip(a, l);
    return r;
}
EXP int accept(int fd, void*a, unsigned*l) {
    static int(*f)(int, void*, unsigned*);
    if (!f) f = nextsym("accept");
    int r = f(fd, a, l);
    if (r >= 0) un_strip(a, l);
    return r;
}
EXP int accept4(int fd, void*a, unsigned*l, int fl) {
    static int(*f)(int, void*, unsigned*, int);
    if (!f) f = nextsym("accept4");
    int r = f(fd, a, l, fl);
    if (r >= 0) un_strip(a, l);
    return r;
}
EXP int socketpair(int domain, int type, int protocol, int sv[2]) {
    static int(*f)(int, int, int, int*);
    if (!f) f = nextsym("socketpair");
    return f ? f(domain, type, protocol, sv) : -1;
}
#define SOL_SOCKET_ 1
#define SO_PASSCRED_ 16
#define SCM_CREDENTIALS_ 2
EXP int setsockopt(int fd, int level, int optname, const void*optval, unsigned int optlen) {
    static int(*real_setsockopt)(int, int, int, const void*, unsigned int);
    if (!real_setsockopt) real_setsockopt = nextsym("setsockopt");
    if (level == SOL_SOCKET_ && optname == SO_PASSCRED_) return 0;
    return real_setsockopt ? real_setsockopt(fd, level, optname, optval, optlen) : 0;
}
EXP int getsockopt(int fd, int level, int optname, void*optval, unsigned int*optlen) {
    static int(*real_getsockopt)(int, int, int, void*, unsigned int*);
    if (!real_getsockopt) real_getsockopt = nextsym("getsockopt");
    if (level == SOL_SOCKET_ && optname == SO_PASSCRED_) {
        if (optval && optlen && *optlen >= sizeof (int)) {
            *(int*) optval = 1;
            *optlen = sizeof (int);
            return 0;
        }
    }
    return real_getsockopt ? real_getsockopt(fd, level, optname, optval, optlen) : 0;
}
struct msghdr_t {
    void*msg_name;
    unsigned int msg_namelen;
    void*msg_iov;
    size_t msg_iovlen;
    void*msg_control;
    size_t msg_controllen;
    int msg_flags;
};
struct cmsghdr_t {
    size_t cmsg_len;
    int cmsg_level;
    int cmsg_type;
};
struct ucred_t {
    u32 pid;
    u32 uid;
    u32 gid;
};
EXP ssize_t sendmsg(int sockfd, const struct msghdr_t*msg, int flags) {
    static ssize_t(*real_sendmsg)(int, const void*, int);
    if (!real_sendmsg) real_sendmsg = nextsym("sendmsg");
    ssize_t ret = real_sendmsg ? real_sendmsg(sockfd, msg, flags) : -1;
    if (ret<0 && *__errno_location() == EPERM) {
        if (msg && msg->msg_iov && msg->msg_iovlen>0) {
            struct iovec_t {
                void*iov_base;
                size_t iov_len;
            }*iov = (struct iovec_t*) msg->msg_iov;
            *__errno_location() = 0;
            return (ssize_t) iov[0].iov_len;
        }
        *__errno_location() = 0;
        return 1;
    }
    return ret;
}
EXP ssize_t recvmsg(int sockfd, struct msghdr_t*msg, int flags) {
    static ssize_t(*real_recvmsg)(int, struct msghdr_t*, int);
    if (!real_recvmsg) real_recvmsg = nextsym("recvmsg");
    ssize_t res = real_recvmsg ? real_recvmsg(sockfd, msg, flags) : -1;
    if (res >= 0 && VROOT() && msg && msg->msg_control && msg->msg_controllen >= sizeof (struct cmsghdr_t)) {
        char*cmsg_ptr = (char*) msg->msg_control;
        size_t clen = msg->msg_controllen;
        while (clen >= sizeof (struct cmsghdr_t)) {
            struct cmsghdr_t*cm = (struct cmsghdr_t*) cmsg_ptr;
            if (cm->cmsg_len<sizeof (struct cmsghdr_t) || cm->cmsg_len>clen) break;
            if (cm->cmsg_level == SOL_SOCKET_ && cm->cmsg_type == SCM_CREDENTIALS_) {
                struct ucred_t*cr = (struct ucred_t*)(cmsg_ptr+sizeof (struct cmsghdr_t));
                if (cm->cmsg_len >= sizeof (struct cmsghdr_t)+sizeof (struct ucred_t)) {
                    cr->uid = 0;
                    cr->gid = 0;
                }
            }
            size_t step = (cm->cmsg_len+sizeof (size_t)-1)&~(sizeof (size_t)-1);
            if (step>clen) break;
            cmsg_ptr += step;
            clen -= step;
        }
    }
    return res;
}
struct cap_header_t {
    u32 version;
    int pid;
};
struct cap_data_t {
    u32 effective;
    u32 permitted;
    u32 inheritable;
};
EXP int capget(struct cap_header_t*hdrp, struct cap_data_t*datap) {
    if (g_user && g_cr[C_EU] != 0) {
        if (datap) {
            memset(datap, 0, sizeof (struct cap_data_t));
            if (hdrp && hdrp->version == 0x20080522) memset(datap+1, 0, sizeof (struct cap_data_t));
        }
        return 0;
    }
    if (VROOT()) {
        if (datap) {
            datap[0].effective = 0xffffffff;
            datap[0].permitted = 0xffffffff;
            datap[0].inheritable = 0xffffffff;
            if (hdrp && hdrp->version == 0x20080522) {
                datap[1].effective = 0xffffffff;
                datap[1].permitted = 0xffffffff;
                datap[1].inheritable = 0xffffffff;
            }
        }
        return 0;
    }
    static int(*f)(struct cap_header_t*, struct cap_data_t*);
    if (!f) f = nextsym("capget");
    return f ? f(hdrp, datap) : 0;
}
EXP int capset(struct cap_header_t*hdrp, const struct cap_data_t*datap) {
    if (g_fake || g_user) return 0;
    static int(*f)(struct cap_header_t*, const struct cap_data_t*);
    if (!f) f = nextsym("capset");
    return f ? f(hdrp, datap) : 0;
}
EXP int prctl(int option, unsigned long a2, unsigned long a3, unsigned long a4, unsigned long a5) {
    if ((g_fake || g_user) && option == 23) return 1;
    if (option == 38) return 0;
    static int(*f)(int, unsigned long, unsigned long, unsigned long, unsigned long);
    if (!f) f = nextsym("prctl");
    return f ? f(option, a2, a3, a4, a5) : 0;
}
struct utsname_t {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};
EXP int uname(struct utsname_t*buf) {
    static int(*f)(struct utsname_t*);
    if (!f) f = nextsym("uname");
    int r = f ? f(buf) : 0;
    if (r == 0 && buf) {
        memcpy(buf->sysname, "Linux", 6);
        memcpy(buf->release, "5.15.0-prx", 11);
        memcpy(buf->version, "#1 SMP PREEMPT", 15);
    }
    return r;
}
EXP int sigaction(int sig, const void*act, void*old) {
    static int(*f)(int, const void*, void*);
    if (!f) f = nextsym("sigaction");
    if (sig == 31 && g_rl && !g_inst) {
        if (old) memset(old, 0, sizeof (struct ksa));
        return 0;
    }
    return f(sig, act, old);
}
EXP void*signal(int sig, void*h) {
    static void*(*f)(int, void*);
    if (!f) f = nextsym("signal");
    if (sig == 31 && g_rl) return NULL;
    return f(sig, h);
}
EXP void*bsd_signal(int sig, void*h) {
    static void*(*f)(int, void*);
    if (!f) f = nextsym("bsd_signal");
    if (sig == 31 && g_rl) return NULL;
    return f(sig, h);
}
EXP int socket(int dom, int type, int proto) {
    static int(*f)(int, int, int);
    if (g_rl && dom == 16 && proto == 9) return (int) fail(93);
    if (!f) f = nextsym("socket");
    return f(dom, type, proto);
}
EXP int lckpwdf(void) {
    static int(*f)(void);
    if (g_rl) return 0;
    if (!f) f = nextsym("lckpwdf");
    return f();
}
EXP int ulckpwdf(void) {
    static int(*f)(void);
    if (g_rl) return 0;
    if (!f) f = nextsym("ulckpwdf");
    return f();
}
EXP int is_selinux_enabled(void) {
    static int(*f)(void);
    if (g_rl) return 0;
    if (!f) f = nextsym("is_selinux_enabled");
    return f ? f() : 0;
}
static const void*unblock_sys(const void*set, int how, u8*tmp) {
    if (!g_rl || !set || (how != 0 && how != 2)) return set;
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
EXP int setfsuid(u32 u) {
    static int(*f)(u32);
    if (g_fake) return 0;
    if (g_user) {
        u32 o = g_cr[C_FU];
        if (g_cr[C_EU] == 0 || u == g_cr[C_RU] || u == g_cr[C_EU] || u == g_cr[C_SU] || u == g_cr[C_FU]) g_cr[C_FU] = u;
        return (int) o;
    }
    if (!f) f = nextsym("setfsuid");
    return f(u);
}
EXP int setfsgid(u32 g) {
    static int(*f)(u32);
    if (g_fake) return 0;
    if (g_user) {
        u32 o = g_cr[C_FG];
        if (g_cr[C_EU] == 0 || g == g_cr[C_RG] || g == g_cr[C_EG] || g == g_cr[C_SG] || g == g_cr[C_FG]) g_cr[C_FG] = g;
        return (int) o;
    }
    if (!f) f = nextsym("setfsgid");
    return f(g);
}
struct sysinfo_data {
    long uptime;
    unsigned long loads[3];
    unsigned long totalram;
    unsigned long freeram;
    unsigned long sharedram;
    unsigned long bufferram;
    unsigned long totalswap;
    unsigned long freeswap;
    unsigned short procs;
    unsigned short pad;
    unsigned long totalhigh;
    unsigned long freehigh;
    unsigned int mem_unit;
    char _f[20-2*sizeof (long)-sizeof (int)];
};
static void sysinfo_limit(struct sysinfo_data*i, int synth) {
    u64 total = g_memkb*1024;
    if (synth) {
        u64 su = 1;
        while (total/su>(u64)(unsigned long)-1) su <<= 1;
        i->mem_unit = (unsigned int) su;
        i->totalram = (unsigned long)(total/su);
        i->freeram = (unsigned long)(total/su/2);
        i->sharedram = 0;
        i->bufferram = 0;
        i->totalswap = 0;
        i->freeswap = 0;
        i->totalhigh = 0;
        i->freehigh = 0;
        return;
    }
    u64 unit = i->mem_unit ? i->mem_unit : 1;
    u64 t = total/unit;
    if (i->totalram>t) i->totalram = t;
    if (i->freeram>i->totalram) i->freeram = i->totalram;
    if (i->sharedram>i->totalram) i->sharedram = i->totalram;
    if (i->bufferram>i->totalram-i->freeram) i->bufferram = i->totalram-i->freeram;
}
EXP int sysinfo(struct sysinfo_data*info) {
    static int(*real_sysinfo)(struct sysinfo_data*);
    if (!real_sysinfo) real_sysinfo = nextsym("sysinfo");
    int res = -1;
    if (real_sysinfo) res = real_sysinfo(info);
    if (info && (res != 0 || info->uptime <= 0)) {
        info->uptime = 43200;
        info->loads[0] = 32768;
        info->loads[1] = 49152;
        info->loads[2] = 52428;
        info->procs = 25;
        if (g_memkb) sysinfo_limit(info, res != 0);
        return 0;
    }
    if (res == 0 && info && g_memkb) sysinfo_limit(info, 0);
    return res;
}
EXP long sysconf(int name) {
    static long(*f)(int);
    if (!f) f = nextsym("sysconf");
    if (g_ncpu>0 && (name == 83 || name == 84)) return g_ncpu;
    long r = f ? f(name) : -1;
    if (g_memkb && (name == 85 || name == 86)) {
        long ps = f ? f(30) : 4096;
        if (ps <= 0) ps = 4096;
        long maxp = (long)(g_memkb*1024/(u64) ps);
        if (name == 85 || r<0 || r>maxp) r = maxp;
    }
    return r;
}
EXP int get_nprocs(void) {
    static int(*f)(void);
    if (g_ncpu>0) return g_ncpu;
    if (!f) f = nextsym("get_nprocs");
    return f ? f() : 1;
}
EXP int get_nprocs_conf(void) {
    static int(*f)(void);
    if (g_ncpu>0) return g_ncpu;
    if (!f) f = nextsym("get_nprocs_conf");
    return f ? f() : 1;
}
#define IDFN0(name,ty,rn,ix) EXP ty name(void){static ty(*f)(void);if(g_fake)return 0;if(g_user)return g_cr[ix];if(!f)f=nextsym(rn);return f();}
IDFN0(getuid, u32, "getuid", C_RU) IDFN0(geteuid, u32, "geteuid", C_EU) IDFN0(getgid, u32, "getgid", C_RG) IDFN0(getegid, u32, "getegid", C_EG)
static int cr_setres(int b, u32 r, u32 e, u32 s) {
    int priv = g_cr[C_EU] == 0;
    u32*c = g_cr+b;
    if (!priv) {
        if (r != CR_NA && r != c[0] && r != c[1] && r != c[2]) return (int) fail(EPERM);
        if (e != CR_NA && e != c[0] && e != c[1] && e != c[2]) return (int) fail(EPERM);
        if (s != CR_NA && s != c[0] && s != c[1] && s != c[2]) return (int) fail(EPERM);
    }
    if (r != CR_NA) c[0] = r;
    if (e != CR_NA) c[1] = e;
    if (s != CR_NA) c[2] = s;
    c[3] = c[1];
    return 0;
}
static int cr_setre(int b, u32 r, u32 e) {
    int priv = g_cr[C_EU] == 0;
    u32*c = g_cr+b;
    u32 ro = c[0];
    if (!priv) {
        if (r != CR_NA && r != c[0] && r != c[1]) return (int) fail(EPERM);
        if (e != CR_NA && e != c[0] && e != c[1] && e != c[2]) return (int) fail(EPERM);
    }
    if (r != CR_NA) c[0] = r;
    if (e != CR_NA) c[1] = e;
    if (r != CR_NA || (e != CR_NA && e != ro)) c[2] = c[1];
    c[3] = c[1];
    return 0;
}
static int cr_setone(int b, u32 v) {
    u32*c = g_cr+b;
    if (g_cr[C_EU] == 0) return cr_setres(b, v, v, v);
    if (v != c[0] && v != c[2]) return (int) fail(EPERM);
    return cr_setres(b, CR_NA, v, CR_NA);
}
EXP int setuid(u32 a) {
    static int(*f)(u32);
    if (g_fake) return 0;
    if (g_user) return cr_setone(0, a);
    if (!f) f = nextsym("setuid");
    return f(a);
}
EXP int setgid(u32 a) {
    static int(*f)(u32);
    if (g_fake) return 0;
    if (g_user) return cr_setone(4, a);
    if (!f) f = nextsym("setgid");
    return f(a);
}
EXP int seteuid(u32 a) {
    static int(*f)(u32);
    if (g_fake) return 0;
    if (g_user) return cr_setres(0, CR_NA, a, CR_NA);
    if (!f) f = nextsym("seteuid");
    return f(a);
}
EXP int setegid(u32 a) {
    static int(*f)(u32);
    if (g_fake) return 0;
    if (g_user) return cr_setres(4, CR_NA, a, CR_NA);
    if (!f) f = nextsym("setegid");
    return f(a);
}
EXP int setreuid(u32 a, u32 b) {
    static int(*f)(u32, u32);
    if (g_fake) return 0;
    if (g_user) return cr_setre(0, a, b);
    if (!f) f = nextsym("setreuid");
    return f(a, b);
}
EXP int setregid(u32 a, u32 b) {
    static int(*f)(u32, u32);
    if (g_fake) return 0;
    if (g_user) return cr_setre(4, a, b);
    if (!f) f = nextsym("setregid");
    return f(a, b);
}
EXP int setresuid(u32 a, u32 b, u32 c) {
    static int(*f)(u32, u32, u32);
    if (g_fake) return 0;
    if (g_user) return cr_setres(0, a, b, c);
    if (!f) f = nextsym("setresuid");
    return f(a, b, c);
}
EXP int setresgid(u32 a, u32 b, u32 c) {
    static int(*f)(u32, u32, u32);
    if (g_fake) return 0;
    if (g_user) return cr_setres(4, a, b, c);
    if (!f) f = nextsym("setresgid");
    return f(a, b, c);
}
static u32 g_fake_groups[NVG] = {0};
static int g_fake_ngroups = 1;
EXP int getgroups(int size, u32*list) {
    if (g_user) {
        if (size == 0) return g_nvg;
        if (size<g_nvg) {
            *__errno_location() = EINVAL;
            return-1;
        }
        for (int i = 0; i<g_nvg; i++) list[i] = g_vg[i];
        return g_nvg;
    }
    if (g_fake) {
        if (size == 0) return g_fake_ngroups;
        if (size<g_fake_ngroups) {
            *__errno_location() = EINVAL;
            return-1;
        }
        for (int i = 0; i<g_fake_ngroups; i++) list[i] = g_fake_groups[i];
        return g_fake_ngroups;
    }
    static int(*f)(int, u32*);
    if (!f) f = nextsym("getgroups");
    return f ? f(size, list) : 0;
}
EXP int setgroups(size_t n, const void*l) {
    if (g_user) {
        if (g_cr[C_EU] != 0) return (int) fail(EPERM);
        if (n>NVG) return (int) fail(EINVAL);
        for (size_t i = 0; i<n; i++) g_vg[i] = ((const u32*) l)[i];
        g_nvg = (int) n;
        return 0;
    }
    if (g_fake) {
        if (n>NVG) return (int) fail(EINVAL);
        for (size_t i = 0; i<n; i++) g_fake_groups[i] = ((const u32*) l)[i];
        g_fake_ngroups = (int) n;
        return 0;
    }
    static int(*f)(size_t, const void*);
    if (!f) f = nextsym("setgroups");
    return f(n, l);
}
EXP int initgroups(const char*u, u32 g) {
    if (g_user) {
        if (g_cr[C_EU] != 0) return (int) fail(EPERM);
        g_nvg = cr_collect(u, g, g_vg, NVG);
        return 0;
    }
    if (g_fake) {
        g_fake_groups[0] = g;
        g_fake_ngroups = 1;
        return 0;
    }
    static int(*f)(const char*, u32);
    if (!f) f = nextsym("initgroups");
    return f(u, g);
}
EXP int getresuid(u32*ruid, u32*euid, u32*suid) {
    if (g_user) {
        if (ruid)*ruid = g_cr[C_RU];
        if (euid)*euid = g_cr[C_EU];
        if (suid)*suid = g_cr[C_SU];
        return 0;
    }
    if (g_fake) {
        if (ruid)*ruid = 0;
        if (euid)*euid = 0;
        if (suid)*suid = 0;
        return 0;
    }
    static int(*f)(u32*, u32*, u32*);
    if (!f) f = nextsym("getresuid");
    return f ? f(ruid, euid, suid) : 0;
}
EXP int getresgid(u32*rgid, u32*egid, u32*sgid) {
    if (g_user) {
        if (rgid)*rgid = g_cr[C_RG];
        if (egid)*egid = g_cr[C_EG];
        if (sgid)*sgid = g_cr[C_SG];
        return 0;
    }
    if (g_fake) {
        if (rgid)*rgid = 0;
        if (egid)*egid = 0;
        if (sgid)*sgid = 0;
        return 0;
    }
    static int(*f)(u32*, u32*, u32*);
    if (!f) f = nextsym("getresgid");
    return f ? f(rgid, egid, sgid) : 0;
}
struct pw {
    char*name, *passwd;
    u32 uid, gid;
    char*gecos, *dir, *shell;
};
struct gr {
    char*name, *passwd;
    u32 gid;
    char**mem;
};
static u32 atou(const char*s) {
    u32 v = 0;
    while (*s >= '0' && *s <= '9') v = v*10+(u32)(*s++-'0');
    return v;
}
/* Read a whole file below the rootfs into a malloc'd, NUL-terminated buffer.
 * Loops until EOF (a single read() may return less than the file size), so
 * there is no fixed size limit other than a sanity cap of 64 MiB. */
#define READ_ALL_MAX (64L*1024*1024)
static char*read_rel_all(const char*rel, long*lenp) {
    char p[PM];
    size_t rl = strlen(rel);
    if (lenp)*lenp = 0;
    if (!g_rl || g_rl+rl+1 >= PM) return NULL;
    memcpy(p, g_root, g_rl);
    memcpy(p+g_rl, rel, rl+1);
    long fd = SC4(N_openat, AT_FDCWD, p, O_RDONLY|O_CLOEXEC, 0);
    if (fd<0) return NULL;
    size_t cap = 65536, len = 0;
    char*b = malloc(cap);
    if (!b) {
        sc(N_close, fd, 0, 0, 0, 0, 0);
        return NULL;
    }
    for (;;) {
        if (len+1 >= cap) {
            if ((long) cap >= READ_ALL_MAX) break;
            char*nb = realloc(b, cap*2);
            if (!nb) break;
            b = nb;
            cap *= 2;
        }
        long n = SC3(N_read, fd, b+len, cap-len-1);
        if (n == -4) continue; /* EINTR */
        if (n <= 0) break;
        len += (size_t) n;
    }
    sc(N_close, fd, 0, 0, 0, 0, 0);
    b[len] = 0;
    if (lenp)*lenp = (long) len;
    return b;
}
static int pw_lookup(const char*name, int byuid, u32 uid, struct pw*o, char*buf, size_t bl) {
    long n;
    char*data = read_rel_all("/etc/passwd", &n);
    if (!data) return 0;
    if (n <= 0) {
        free(data);
        return 0;
    }
    int res = 0;
    for (char*l = data; *l; ) {
        char*e = l;
        while (*e && *e != '\n') e++;
        char sv = *e;
        *e = 0;
        char*f[7];
        int k = 0;
        f[k++] = l;
        for (char*p = l; *p && k<7; p++) {
            if (*p == ':') {
                *p = 0;
                f[k++] = p+1;
            }
        }
        if (k == 7 && ((byuid && atou(f[2]) == uid) || (!byuid && !strcmp(f[0], name)))) {
            size_t need = 0;
            for (int i = 0; i<7; i++) need += strlen(f[i])+1;
            if (need>bl) {
                res = -1;
                break;
            }
            char*d = buf, *ptr[7];
            for (int i = 0; i<7; i++) {
                size_t z = strlen(f[i])+1;
                memcpy(d, f[i], z);
                ptr[i] = d;
                d += z;
            }
            o->name = ptr[0];
            o->passwd = ptr[1];
            o->uid = atou(ptr[2]);
            o->gid = atou(ptr[3]);
            o->gecos = ptr[4];
            o->dir = ptr[5];
            o->shell = ptr[6];
            res = 1;
            break;
        }
        *e = sv;
        l = sv ? e+1 : e;
    }
    free(data);
    return res;
}
static int gr_lookup(const char*name, int bygid, u32 gid, struct gr*o, char*buf, size_t bl) {
    long n;
    char*data = read_rel_all("/etc/group", &n);
    if (!data) return 0;
    if (n <= 0) {
        free(data);
        return 0;
    }
    int res = 0;
    for (char*l = data; *l; ) {
        char*e = l;
        while (*e && *e != '\n') e++;
        char sv = *e;
        *e = 0;
        char*f[4];
        int k = 0;
        f[k++] = l;
        for (char*p = l; *p && k<4; p++) {
            if (*p == ':') {
                *p = 0;
                f[k++] = p+1;
            }
        }
        if (k == 4 && ((bygid && atou(f[2]) == gid) || (!bygid && !strcmp(f[0], name)))) {
            size_t nmem = 0;
            if (f[3][0]) {
                nmem = 1;
                for (const char*c = f[3]; *c; c++) if (*c == ',') nmem++;
            }
            size_t need = (nmem+1)*sizeof (char*)+8;
            for (int i = 0; i<4; i++) need += strlen(f[i])+1;
            if (need>bl) {
                res = -1;
                break;
            }
            char**arr = (char**)(((unsigned long) buf+7)&~7UL);
            char*d = (char*)(arr+nmem+1);
            char*ptr[4];
            for (int i = 0; i<4; i++) {
                size_t z = strlen(f[i])+1;
                memcpy(d, f[i], z);
                ptr[i] = d;
                d += z;
            }
            int m = 0;
            char*p = ptr[3];
            while (*p && (size_t) m<nmem) {
                arr[m++] = p;
                while (*p && *p != ',') p++;
                if (*p)*p++ = 0;
            }
            arr[m] = NULL;
            o->name = ptr[0];
            o->passwd = ptr[1];
            o->gid = atou(ptr[2]);
            o->mem = arr;
            res = 1;
            break;
        }
        *e = sv;
        l = sv ? e+1 : e;
    }
    free(data);
    return res;
}
static char*cr_put(char*p, u32 v) {
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0'+v%10);
        v /= 10;
    } while (v);
    while (n)*p++ = t[--n];
    return p;
}
static void cr_encode(const u32*c, char*out) {
    char*p = out;
    for (int i = 0; i<8; i++) {
        p = cr_put(p, c[i]);
        *p++ = (i<7) ? ',' : ':';
    }
    for (int i = 0; i<g_nvg; i++) {
        if (i)*p++ = ',';
        p = cr_put(p, g_vg[i]);
    }
    *p = 0;
}
static const char*cr_get(const char*p, u32*v) {
    if (*p<'0' || *p>'9') return NULL;
    u32 r = 0;
    while (*p >= '0' && *p <= '9') r = r*10+(u32)(*p++-'0');
    *v = r;
    return p;
}
static int cr_decode(const char*s) {
    u32 t[8], gg[NVG];
    int ng = 0;
    for (int i = 0; i<8; i++) {
        s = cr_get(s, &t[i]);
        if (!s) return 0;
        if (*s != ((i<7) ? ',' : ':')) return 0;
        s++;
    }
    while (*s && ng<NVG) {
        u32 v;
        s = cr_get(s, &v);
        if (!s) break;
        gg[ng++] = v;
        if (*s == ',') s++;
        else break;
    }
    for (int i = 0; i<8; i++) g_cr[i] = t[i];
    for (int i = 0; i<ng; i++) g_vg[i] = gg[i];
    g_nvg = ng;
    return 1;
}
static int cr_collect(const char*user, u32 gid, u32*out, int max) {
    int n = 0;
    if (max > 0) out[n++] = gid;
    long len;
    char*gdata = read_rel_all("/etc/group", &len);
    if (!gdata) return n;
    if (len <= 0) {
        free(gdata);
        return n;
    }
    size_t ul = strlen(user);
    for (char*l = gdata; *l; ) {
        char*e = l;
        while (*e && *e != '\n') e++;
        char sv = *e;
        *e = 0;
        char*f[4];
        int k = 0;
        f[k++] = l;
        for (char*p = l; *p && k<4; p++) {
            if (*p == ':') {
                *p = 0;
                f[k++] = p+1;
            }
        }
        if (k == 4) {
            int hit = 0;
            const char*m = f[3];
            while (*m) {
                const char*q = m;
                while (*q && *q != ',') q++;
                if ((size_t)(q-m) == ul && !memcmp(m, user, ul)) hit = 1;
                m = *q ? q+1 : q;
            }
            if (hit) {
                u32 g = atou(f[2]);
                int dup = 0;
                for (int i = 0; i<n; i++) if (out[i] == g) dup = 1;
                if (!dup && n<max) out[n++] = g;
            }
        }
        *e = sv;
        l = sv ? e+1 : e;
    }
    free(gdata);
    return n;
}
static void cred_init(void) {
    if (g_credbuf[0] && cr_decode(g_credbuf)) return;
    for (int i = 0; i<4; i++) g_cr[i] = g_ruid;
    for (int i = 4; i<8; i++) g_cr[i] = g_rgid;
    g_nvg = cr_collect(g_vuser, g_rgid, g_vg, NVG);
}
/* Non-reentrant getpw*()/getgr*() results live in heap buffers that grow on
 * demand, so large entries (e.g. groups with hundreds of members) are never
 * truncated. */
static char*s_pwbuf, *s_grbuf;
static size_t s_pwcap, s_grcap;
static struct pw s_pw;
static struct gr s_gr;
static int pw_lookup_dyn(const char*name, int byuid, u32 uid) {
    if (!s_pwbuf) {
        s_pwbuf = malloc(s_pwcap = 2048);
        if (!s_pwbuf) {
            s_pwcap = 0;
            return-1;
        }
    }
    for (;;) {
        int r = pw_lookup(name, byuid, uid, &s_pw, s_pwbuf, s_pwcap);
        if (r >= 0 || s_pwcap >= (1UL<<26)) return r;
        char*nb = realloc(s_pwbuf, s_pwcap*2);
        if (!nb) return-1;
        s_pwbuf = nb;
        s_pwcap *= 2;
    }
}
static int gr_lookup_dyn(const char*name, int bygid, u32 gid) {
    if (!s_grbuf) {
        s_grbuf = malloc(s_grcap = 4096);
        if (!s_grbuf) {
            s_grcap = 0;
            return-1;
        }
    }
    for (;;) {
        int r = gr_lookup(name, bygid, gid, &s_gr, s_grbuf, s_grcap);
        if (r >= 0 || s_grcap >= (1UL<<26)) return r;
        char*nb = realloc(s_grbuf, s_grcap*2);
        if (!nb) return-1;
        s_grbuf = nb;
        s_grcap *= 2;
    }
}
EXP struct pw*getpwuid(u32 uid) {
    static struct pw*(*f)(u32);
    if (g_rl && pw_lookup_dyn(NULL, 1, uid) == 1) return&s_pw;
    if (!f) f = nextsym("getpwuid");
    return f(uid);
}
EXP struct pw*getpwnam(const char*n) {
    static struct pw*(*f)(const char*);
    if (g_rl && pw_lookup_dyn(n, 0, 0) == 1) return&s_pw;
    if (!f) f = nextsym("getpwnam");
    return f(n);
}
EXP struct gr*getgrgid(u32 gid) {
    static struct gr*(*f)(u32);
    if (g_rl && gr_lookup_dyn(NULL, 1, gid) == 1) return&s_gr;
    if (!f) f = nextsym("getgrgid");
    return f(gid);
}
EXP struct gr*getgrnam(const char*n) {
    static struct gr*(*f)(const char*);
    if (g_rl && gr_lookup_dyn(n, 0, 0) == 1) return&s_gr;
    if (!f) f = nextsym("getgrnam");
    return f(n);
}
EXP int getpwnam_r(const char*n, struct pw*pw, char*buf, size_t bl, struct pw**res) {
    static int(*f)(const char*, struct pw*, char*, size_t, struct pw**);
    if (g_rl) {
        int r = pw_lookup(n, 0, 0, pw, buf, bl);
        if (r == 1) {
            *res = pw;
            return 0;
        }
        if (r<0) {
            *res = NULL;
            return ERANGE;
        }
    }
    if (!f) f = nextsym("getpwnam_r");
    return f(n, pw, buf, bl, res);
}
EXP int getpwuid_r(u32 uid, struct pw*pw, char*buf, size_t bl, struct pw**res) {
    static int(*f)(u32, struct pw*, char*, size_t, struct pw**);
    if (g_rl) {
        int r = pw_lookup(NULL, 1, uid, pw, buf, bl);
        if (r == 1) {
            *res = pw;
            return 0;
        }
        if (r<0) {
            *res = NULL;
            return ERANGE;
        }
    }
    if (!f) f = nextsym("getpwuid_r");
    return f(uid, pw, buf, bl, res);
}
EXP int getgrnam_r(const char*n, struct gr*gr, char*buf, size_t bl, struct gr**res) {
    static int(*f)(const char*, struct gr*, char*, size_t, struct gr**);
    if (g_rl) {
        int r = gr_lookup(n, 0, 0, gr, buf, bl);
        if (r == 1) {
            *res = gr;
            return 0;
        }
        if (r<0) {
            *res = NULL;
            return ERANGE;
        }
    }
    if (!f) f = nextsym("getgrnam_r");
    return f(n, gr, buf, bl, res);
}
EXP int getgrgid_r(u32 gid, struct gr*gr, char*buf, size_t bl, struct gr**res) {
    static int(*f)(u32, struct gr*, char*, size_t, struct gr**);
    if (g_rl) {
        int r = gr_lookup(NULL, 1, gid, gr, buf, bl);
        if (r == 1) {
            *res = gr;
            return 0;
        }
        if (r<0) {
            *res = NULL;
            return ERANGE;
        }
    }
    if (!f) f = nextsym("getgrgid_r");
    return f(gid, gr, buf, bl, res);
}
struct ar {
    void*p[256];
    int n;
};
static void*am(struct ar*a, size_t n) {
    if (a->n >= 256) return NULL;
    void*p = malloc(n ? n : 1);
    if (p) a->p[a->n++] = p;
    return p;
}
static void afree(struct ar*a) {
    while (a->n) free(a->p[--a->n]);
}
static char*acat(struct ar*a, const char*k, const char*v) {
    size_t kl = strlen(k), vl = strlen(v);
    char*d = am(a, kl+vl+1);
    if (d) {
        memcpy(d, k, kl);
        memcpy(d+kl, v, vl+1);
    }
    return d;
}
static int is_ours(const char*e) {
    return envmatch(e, "LD_PRELOAD") || envmatch(e, "PRX_ROOTFS") || envmatch(e, "PRX_SHIM") || envmatch(e, "PRX_LIBPATH") || envmatch(e, "PRX_BIND") || envmatch(e, "PRX_FAKEROOT") || envmatch(e, "PRX_DNS") || envmatch(e, "PRX_LOADER") || envmatch(e, "PRX_QEMU") || envmatch(e, "QEMU_SET_ENV") || envmatch(e, "PRX_SELF_EXE") || envmatch(e, "PRX_VUSER") || envmatch(e, "PRX_CRED") || envmatch(e, "PRX_CPUS") || envmatch(e, "PRX_MEM_KB") || envmatch(e, "PRX_NO_ARGV0");
}
#if defined(__i386__)
/* ELF32 (the only class an i386 guest can run) */
#define ELF_CLASS 1
#define ELF_PHMIN 32
#define EH_PHOFF(h) ((u64)*(const u32*)((h)+28))
#define EH_PHES(h) (*(const u16*)((h)+42))
#define EH_PHN(h) (*(const u16*)((h)+44))
#define PH_OFF(p) ((u64)*(const u32*)((p)+4))
#define PH_VADDR(p) ((u64)*(const u32*)((p)+8))
#define PH_FILESZ(p) ((u64)*(const u32*)((p)+16))
#define DYN_SZ 8
#define DYN_TAG(d) ((long)*(const int*)(d))
#define DYN_VAL(d) ((u64)*(const u32*)((d)+4))
#else
#define ELF_CLASS 2
#define ELF_PHMIN 56
#define EH_PHOFF(h) (*(const u64*)((h)+32))
#define EH_PHES(h) (*(const u16*)((h)+54))
#define EH_PHN(h) (*(const u16*)((h)+56))
#define PH_OFF(p) (*(const u64*)((p)+8))
#define PH_VADDR(p) (*(const u64*)((p)+16))
#define PH_FILESZ(p) (*(const u64*)((p)+32))
#define DYN_SZ 16
#define DYN_TAG(d) (*(const long*)(d))
#define DYN_VAL(d) (*(const u64*)((d)+8))
#endif
static int elf_runpath(int fd, const u8*ph, unsigned phes, unsigned phn, char*out, size_t osz) {
    u64 dyn_off = 0, dyn_sz = 0;
    out[0] = 0;
    for (unsigned i = 0; i<phn; i++) {
        const u8*p = ph+phes*i;
        if (*(const u32*) p == 2) {
            dyn_off = PH_OFF(p);
            dyn_sz = PH_FILESZ(p);
            break;
        }
    }
    if (!dyn_sz) return 0;
    if (dyn_sz>4096) dyn_sz = 4096;
    u8 db[4096];
    long dn = pread_(fd, db, (long) dyn_sz, dyn_off);
    if (dn<DYN_SZ) return 0;
    u64 strtab = 0, rp = 0, rpath = 0;
    int hr = 0, hp = 0;
    for (long i = 0; i+DYN_SZ <= dn; i += DYN_SZ) {
        long tag = DYN_TAG(db+i);
        u64 v = DYN_VAL(db+i);
        if (tag == 0) break;
        if (tag == 5) strtab = v;
        else if (tag == 29) {
            rp = v;
            hr = 1;
        } else if (tag == 15) {
            rpath = v;
            hp = 1;
        }
    }
    if (!strtab || (!hr && !hp)) return 0;
    u64 off = 0;
    int found = 0;
    for (unsigned i = 0; i<phn && !found; i++) {
        const u8*p = ph+phes*i;
        if (*(const u32*) p != 1) continue;
        u64 po = PH_OFF(p), va = PH_VADDR(p), fs = PH_FILESZ(p);
        if (strtab >= va && strtab<va+fs) {
            off = strtab-va+po;
            found = 1;
        }
    }
    if (!found) return 0;
    long rn = pread_(fd, out, (long)(osz-1), off+(hr ? rp : rpath));
    if (rn <= 0) return 0;
    out[rn] = 0;
    return out[0] != 0;
}
static const char*build_libpath(struct ar*a, const char*rp) {
    if (!rp || !rp[0] || !g_rl) return g_libpath;
    size_t gl = strlen(g_libpath), tot = gl+1;
    for (const char*q = rp; *q; ) {
        const char*e = q;
        while (*e && *e != ':') e++;
        if (*q == '/') tot += g_rl+(size_t)(e-q)+1;
        q = *e ? e+1 : e;
    }
    char*o = am(a, tot);
    if (!o) return g_libpath;
    size_t k = 0;
    for (const char*q = rp; *q; ) {
        const char*e = q;
        while (*e && *e != ':') e++;
        if (*q == '/') {
            memcpy(o+k, g_root, g_rl);
            k += g_rl;
            memcpy(o+k, q, (size_t)(e-q));
            k += (size_t)(e-q);
            o[k++] = ':';
        }
        q = *e ? e+1 : e;
    }
    memcpy(o+k, g_libpath, gl+1);
    return o;
}
static const char*strip_shim(struct ar*a, const char*up) {
    size_t sl = strlen(g_shim), ul = strlen(up);
    char*o = am(a, ul+1);
    if (!o) return up;
    size_t k = 0;
    const char*q = up;
    while (*q) {
        const char*e = q;
        while (*e && *e != ':') e++;
        size_t l = (size_t)(e-q);
        if (l && !(l == sl && memcmp(q, g_shim, sl) == 0)) {
            if (k) o[k++] = ':';
            memcpy(o+k, q, l);
            k += l;
        }
        q = *e ? e+1 : e;
    }
    o[k] = 0;
    return o;
}
static void exec_cred_prepare(const char*h) {
    g_pcr_ok = 0;
    if (!g_user) return;
    memcpy(g_pcr, g_cr, sizeof g_pcr);
    char st[256];
    if (SC4(N_newfstatat, AT_FDCWD, h, st, 0) == 0) {
        u32 mode = *(const u32*)(st+MODE_OFF);
        u32 fu = *(const u32*)(st+OFF_UID), fg = *(const u32*)(st+OFF_UID+4);
        if ((mode&0170000) == 0100000 && (mode&06000) && fu == g_ruid) {
            int rt = own_is_root_host(h);
            if (mode&04000) g_pcr[C_EU] = rt ? 0 : fu;
            if ((mode&02000) && (mode&0010)) g_pcr[C_EG] = rt ? 0 : fg;
        }
    }
    g_pcr[C_SU] = g_pcr[C_FU] = g_pcr[C_EU];
    g_pcr[C_SG] = g_pcr[C_FG] = g_pcr[C_EG];
    g_pcr_ok = 1;
}
static char**mkenv(struct ar*a, char*const envp[], int native, const char*cur_exe) {
    int n = 0;
    while (envp && envp[n]) n++;
    char**ne = am(a, (size_t)(n+24)*sizeof (char*));
    if (!ne) return NULL;
    int k = 0;
    const char*user_preload = NULL;
    for (int i = 0; i<n; i++) {
        if (envmatch(envp[i], "LD_PRELOAD")) user_preload = envp[i]+11;
        if (!is_ours(envp[i])) ne[k++] = envp[i];
    }
    if (user_preload && user_preload[0]) user_preload = strip_shim(a, user_preload);
    if (!native) {
        if (user_preload && user_preload[0]) {
            char*t1 = acat(a, g_shim, ":");
            char*t2 = acat(a, t1, user_preload);
            ne[k++] = acat(a, "LD_PRELOAD=", t2);
        } else {
            ne[k++] = acat(a, "LD_PRELOAD=", g_shim);
        }
        ne[k++] = acat(a, "PRX_ROOTFS=", g_root);
        ne[k++] = acat(a, "PRX_SHIM=", g_shim);
        ne[k++] = acat(a, "PRX_LIBPATH=", g_libpath);
        ne[k++] = acat(a, "PRX_BIND=", g_bindenv);
        ne[k++] = acat(a, "PRX_DNS=", g_dns);
        ne[k++] = acat(a, "PRX_FAKEROOT=", g_fake ? "1" : "0");
        if (g_ncpu>0) {
            char nb[24];
            u64_dec(nb, (u64) g_ncpu);
            ne[k++] = acat(a, "PRX_CPUS=", nb);
        }
        if (g_memkb) {
            char nb[24];
            u64_dec(nb, g_memkb);
            ne[k++] = acat(a, "PRX_MEM_KB=", nb);
        }
        if (g_user) {
            char cb[4096];
            cr_encode(g_pcr_ok ? g_pcr : g_cr, cb);
            ne[k++] = acat(a, "PRX_VUSER=", g_vuser);
            ne[k++] = acat(a, "PRX_CRED=", cb);
        }
        if (cur_exe && cur_exe[0]) ne[k++] = acat(a, "PRX_SELF_EXE=", cur_exe);
        if (g_loader[0]) ne[k++] = acat(a, "PRX_LOADER=", g_loader);
        if (g_noargv0) ne[k++] = acat(a, "PRX_NO_ARGV0=", "1");
        if (g_qemu[0]) ne[k++] = acat(a, "PRX_QEMU=", g_qemu);
        int has = 0;
        for (int i = 0; i<n; i++) if (envmatch(envp[i], "GLIBC_TUNABLES")) has = 1;
        if (!has) ne[k++] = acat(a, "GLIBC_TUNABLES=", "glibc.pthread.rseq=0");
    }
    if (!native && g_qemu[0]) {
        const char*pl = NULL, *ll = NULL;
        int j = 0;
        for (int i = 0; i<k; i++) {
            if (envmatch(ne[i], "LD_PRELOAD")) pl = ne[i];
            else if (envmatch(ne[i], "LD_LIBRARY_PATH")) ll = ne[i];
            else ne[j++] = ne[i];
        }
        k = j;
        if (pl || ll) {
            char*se = acat(a, "QEMU_SET_ENV=", pl ? pl : ll);
            if (se && pl && ll) {
                char*t = acat(a, se, ",");
                se = t ? acat(a, t, ll) : NULL;
            }
            ne[k++] = se;
        }
    }
    ne[k] = NULL;
    for (int i = 0; i<k; i++) if (!ne[i]) return NULL;
    return ne;
}
static int prep_exec(struct ar*a, const char*path, char*const argv[], char*const envp[], const char**op, char***oa, char***oe) {
    int argc = 0;
    while (argv && argv[argc]) argc++;
    char**av = am(a, (size_t)(argc+8)*sizeof (char*));
    if (!av) return (int) fail(ENOMEM);
    g_pcr_ok = 0;
    for (int i = 0; i<argc; i++) av[i] = argv[i];
    av[argc] = NULL;
    if (!argc) {
        av[0] = (char*) path;
        av[1] = NULL;
        argc = 1;
    }
    if (!g_rl) {
        *op = path;
        *oa = av;
        *oe = (char**) envp;
        return 0;
    }
    char gpath[PM];
    const char*gp = path;
    if (g_self_exe[0] && is_proc_exe(gp)) {
        gp = g_self_exe;
        if (argc>0 && is_proc_exe(av[0])) av[0] = (char*) g_self_exe;
    } else if (gp[0] != '/') {
        if (mkabs(AT_FDCWD, gp, gpath) == 0) gp = gpath;
    }
    for (int depth = 0; depth<6; depth++) {
        char*hb = am(a, PM);
        const char*h;
        if (!hb) return (int) fail(ENOMEM);
        if (tr(AT_FDCWD, gp, hb, 1, &h)<0) return-1;
        if (h == gp) {
            *op = h;
            *oa = av;
            *oe = mkenv(a, envp, 1, gp);
            return*oe ? 0 : (int) fail(ENOMEM);
        }
        long r = SC3(N_faccessat, AT_FDCWD, h, 1);
        if (r<0) return (int) ret(r);
        long fd = SC4(N_openat, AT_FDCWD, h, O_RDONLY|O_CLOEXEC, 0);
        if (fd<0) return (int) ret(fd);
        u8 hd[512];
        long hn = SC3(N_read, fd, hd, sizeof hd);
        if (hn<4) {
            sc(N_close, fd, 0, 0, 0, 0, 0);
            return (int) fail(ENOEXEC);
        }
        if (hd[0] == '#' && hd[1] == '!') {
            sc(N_close, fd, 0, 0, 0, 0, 0);
            long i = 2, e;
            while (i<hn && (hd[i] == ' ' || hd[i] == '\t')) i++;
            long s = i;
            while (i<hn && hd[i] != ' ' && hd[i] != '\t' && hd[i] != '\n') i++;
            if (i == s) return (int) fail(ENOEXEC);
            char*interp = am(a, (size_t)(i-s)+1);
            if (!interp) return (int) fail(ENOMEM);
            memcpy(interp, hd+s, (size_t)(i-s));
            interp[i-s] = 0;
            while (i<hn && (hd[i] == ' ' || hd[i] == '\t')) i++;
            s = i;
            while (i<hn && hd[i] != '\n') i++;
            e = i;
            while (e>s && (hd[e-1] == ' ' || hd[e-1] == '\t' || hd[e-1] == '\r')) e--;
            char*iarg = NULL;
            if (e>s) {
                iarg = am(a, (size_t)(e-s)+1);
                if (!iarg) return (int) fail(ENOMEM);
                memcpy(iarg, hd+s, (size_t)(e-s));
                iarg[e-s] = 0;
            }
            char**nv = am(a, (size_t)(argc+8)*sizeof (char*));
            if (!nv) return (int) fail(ENOMEM);
            int k = 0;
            nv[k++] = interp;
            if (iarg) nv[k++] = iarg;
            nv[k++] = (char*) gp;
            for (int j = 1; j<argc; j++) nv[k++] = av[j];
            nv[k] = NULL;
            av = nv;
            argc = k;
            gp = interp;
            continue;
        }
        if (!(hd[0] == 0x7f && hd[1] == 'E' && hd[2] == 'L' && hd[3] == 'F') || hn<(ELF_CLASS == 1 ? 52 : 64) || hd[4] != ELF_CLASS) {
            sc(N_close, fd, 0, 0, 0, 0, 0);
            return (int) fail(ENOEXEC);
        }
        u64 phoff = EH_PHOFF(hd);
        unsigned phes = EH_PHES(hd), phn = EH_PHN(hd);
        char interp[256];
        char rpbuf[1024];
        int have = 0;
        rpbuf[0] = 0;
        if (phes >= ELF_PHMIN && phes <= 64 && phn && phn <= 40) {
            u8 ph[64*40];
            long pn = pread_(fd, ph, (long)(phes*phn), phoff);
            if (pn >= (long)(phes*phn)) elf_runpath((int) fd, ph, phes, phn, rpbuf, sizeof rpbuf);
            for (unsigned i = 0; pn >= (long)(phes*(i+1)) && i<phn; i++) {
                u8*p = ph+phes*i;
                if (*(u32*) p == 3) {
                    u64 off = PH_OFF(p), sz = PH_FILESZ(p);
                    if (sz>0 && sz<sizeof interp) {
                        long rn = pread_(fd, interp, (long) sz, off);
                        if (rn>0) {
                            interp[sz] = 0;
                            have = 1;
                        }
                    }
                    break;
                }
            }
        }
        sc(N_close, fd, 0, 0, 0, 0, 0);
        if (!have && g_rl) {
            const char*bn = gp;
            for (const char*q = gp; *q; q++) if (*q == '/') bn = q+1;
            if (strcmp(bn, "ldconfig") == 0 || strcmp(bn, "ldconfig.real") == 0) {
                char**nv = am(a, 2*sizeof (char*));
                if (!nv) return (int) fail(ENOMEM);
                nv[0] = "true";
                nv[1] = NULL;
                av = nv;
                argc = 1;
                gp = "/bin/true";
                continue;
            }
        }
        if (!have) {
            if (g_qemu[0]) {
                char**qv = am(a, (size_t)(argc+8)*sizeof (char*));
                if (!qv) return (int) fail(ENOMEM);
                int qk = 0;
                qv[qk++] = (char*) g_qemu;
                qv[qk++] = "-0";
                qv[qk++] = av[0];
                qv[qk++] = (char*) h;
                for (int j = 1; j<argc; j++) qv[qk++] = av[j];
                qv[qk] = NULL;
                *op = g_qemu;
                *oa = qv;
            } else {
                *op = h;
                *oa = av;
            }
            exec_cred_prepare(h);
            *oe = mkenv(a, envp, 0, gp);
            return*oe ? 0 : (int) fail(ENOMEM);
        }
        char*lb = am(a, PM);
        const char*lh;
        if (!lb) return (int) fail(ENOMEM);
        if (tr(AT_FDCWD, interp, lb, 1, &lh)<0) return-1;
        char**nv = am(a, (size_t)(argc+8)*sizeof (char*));
        if (!nv) return (int) fail(ENOMEM);
        int k = 0;
        if (g_loader[0]) lh = g_loader;
        if (g_qemu[0]) nv[k++] = (char*) g_qemu;
        nv[k++] = (char*) lh;
        nv[k++] = "--library-path";
        nv[k++] = (char*) build_libpath(a, rpbuf);
        if (!g_noargv0) {
            nv[k++] = "--argv0";
            nv[k++] = av[0];
        }
        nv[k++] = (char*) h;
        for (int j = 1; j<argc; j++) nv[k++] = av[j];
        nv[k] = NULL;
        *op = g_qemu[0] ? g_qemu : lh;
        *oa = nv;
        exec_cred_prepare(h);
        *oe = mkenv(a, envp, 0, gp);
        return*oe ? 0 : (int) fail(ENOMEM);
    }
    return (int) fail(ELOOP);
}
static int do_execve(const char*path, char*const argv[], char*const envp[]) {
    struct ar a;
    a.n = 0;
    const char*p;
    char**av, **ev;
    if (prep_exec(&a, path, argv, envp, &p, &av, &ev)<0) {
        afree(&a);
        return-1;
    }
    long r = SC3(N_execve, p, av, ev);
    afree(&a);
    return (int) ret(r);
}
EXP int execve(const char*p, char*const a[], char*const e[]) {
    return do_execve(p, a, e);
}
static int exec_hostpath(const char*hp, char*const argv[], char*const envp[]) {
    char rb[PM];
    const char*g = hp;
    if (hasroot(hp)) {
        g = hp+g_rl;
        if (!*g) g = "/";
    } else {
        const char*r = rev_bind(hp, rb);
        if (r) g = r;
    }
    return do_execve(g, argv, envp);
}
EXP int execveat(int dirfd, const char*pathname, char*const argv[], char*const envp[], int flags) {
    char abs_path[PM];
    char fd_path[32];
    int empty = (flags&0x1000) && (!pathname || !*pathname);
    if (!empty && !pathname) return (int) fail(EFAULT);
    if (!empty && pathname && pathname[0] == '/') {
        return do_execve(pathname, argv, envp);
    }
    if (!empty && dirfd == AT_FDCWD) {
        return do_execve(pathname, argv, envp);
    }
    if (empty && dirfd<0) return (int) fail(EINVAL);
    fdpath(fd_path, dirfd);
    long n = ret(sc(N_readlinkat, AT_FDCWD, (long) fd_path, (long) abs_path, PM-1, 0, 0));
    if (n>0) {
        abs_path[n] = 0;
        if (empty) return exec_hostpath(abs_path, argv, envp);
        abs_path[n] = '/';
        size_t len = strlen(pathname);
        if ((size_t) n+1+len<PM) {
            memcpy(abs_path+n+1, pathname, len+1);
            return exec_hostpath(abs_path, argv, envp);
        }
        return (int) fail(ENAMETOOLONG);
    }
    return (int) fail(ENOENT);
}
EXP int execv(const char*p, char*const a[]) {
    return do_execve(p, a, environ);
}
static int search_exec(const char*file, char*const argv[], char*const envp[]) {
    if (!file || !*file) return (int) fail(ENOENT);
    size_t fl = strlen(file);
    for (size_t i = 0; i<fl; i++) if (file[i] == '/') goto direct;
    {
        const char*path = getenv("PATH");
        if (!path) path = "/usr/local/bin:/usr/bin:/bin";
        int eacces = 0;
        while (1) {
            const char*q = path;
            while (*q && *q != ':') q++;
            size_t dl = (size_t)(q-path);
            if (dl+fl+2<PM) {
                char c[PM];
                if (dl) {
                    memcpy(c, path, dl);
                    c[dl++] = '/';
                }
                memcpy(c+dl, file, fl+1);
                do_execve(c, argv, envp);
                int e = geterr();
                if (e == ENOEXEC) {
                    int n = 0;
                    while (argv && argv[n]) n++;
                    char*nv[n+3];
                    nv[0] = "sh";
                    nv[1] = c;
                    for (int j = 1; j<n; j++) nv[j+1] = argv[j];
                    nv[n>0 ? n+1 : 2] = NULL;
                    do_execve("/bin/sh", nv, envp);
                    return-1;
                }
                if (e == EACCES) eacces = 1;
                else if (e != ENOENT && e != ENOTDIR && e != ESTALE) return-1;
            }
            if (!*q) break;
            path = q+1;
        }
        return (int) fail(eacces ? EACCES : ENOENT);
    }
    direct : do_execve(file, argv, envp);
    if (geterr() == ENOEXEC) {
        int n = 0;
        while (argv && argv[n]) n++;
        char*nv[n+3];
        nv[0] = "sh";
        nv[1] = (char*) file;
        for (int j = 1; j<n; j++) nv[j+1] = argv[j];
        nv[n>0 ? n+1 : 2] = NULL;
        do_execve("/bin/sh", nv, envp);
    }
    return-1;
}
EXP int execvp(const char*f, char*const a[]) {
    return search_exec(f, a, environ);
}
EXP int execvpe(const char*f, char*const a[], char*const e[]) {
    return search_exec(f, a, e);
}
static char**collect(const char*first, __builtin_va_list*app, int*n) {
    int c = 0;
    __builtin_va_list cp;
    __builtin_va_copy(cp, *app);
    if (first) {
        c = 1;
        while (__builtin_va_arg(cp, char*)) c++;
    }
    __builtin_va_end(cp);
    char**v = malloc((size_t)(c+1)*sizeof (char*));
    if (!v) return NULL;
    int i = 0;
    if (first) {
        v[i++] = (char*) first;
        while (i<c) v[i++] = __builtin_va_arg(*app, char*);
        (void) __builtin_va_arg(*app, char*);
    }
    v[i] = NULL;
    *n = i;
    return v;
}
EXP int execl(const char*path, const char*a0, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, a0);
    int n;
    char**v = collect(a0, &ap, &n);
    __builtin_va_end(ap);
    if (!v) return (int) fail(ENOMEM);
    int r = do_execve(path, v, environ);
    free(v);
    return r;
}
EXP int execlp(const char*file, const char*a0, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, a0);
    int n;
    char**v = collect(a0, &ap, &n);
    __builtin_va_end(ap);
    if (!v) return (int) fail(ENOMEM);
    int r = search_exec(file, v, environ);
    free(v);
    return r;
}
EXP int execle(const char*path, const char*a0, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, a0);
    int n;
    char**v = collect(a0, &ap, &n);
    char**envp = __builtin_va_arg(ap, char**);
    __builtin_va_end(ap);
    if (!v) return (int) fail(ENOMEM);
    int r = do_execve(path, v, envp);
    free(v);
    return r;
}
typedef int(*spawn_fn)(int*, const char*, const void*, const void*, char*const[], char*const[]);
static int do_spawn(int*pid, const char*path, const void*fa, const void*attr, char*const argv[], char*const envp[]) {
    static spawn_fn real;
    struct ar a;
    a.n = 0;
    const char*p;
    char**av, **ev;
    if (!real) real = (spawn_fn) nextsym("posix_spawn");
    if (prep_exec(&a, path, argv, envp, &p, &av, &ev)<0) {
        int e = geterr();
        afree(&a);
        return e;
    }
    int r = real(pid, p, fa, attr, av, ev);
    afree(&a);
    return r;
}
EXP int posix_spawn(int*pid, const char*p, const void*fa, const void*at, char*const a[], char*const e[]) {
    return do_spawn(pid, p, fa, at, a, e);
}
EXP int posix_spawnp(int*pid, const char*file, const void*fa, const void*at, char*const a[], char*const e[]) {
    size_t fl = strlen(file);
    for (size_t i = 0; i<fl; i++) if (file[i] == '/') return do_spawn(pid, file, fa, at, a, e);
    const char*path = getenv("PATH");
    if (!path) path = "/usr/local/bin:/usr/bin:/bin";
    int eacces = 0;
    for (;; ) {
        const char*q = path;
        while (*q && *q != ':') q++;
        size_t dl = (size_t)(q-path);
        if (dl+fl+2<PM) {
            char c[PM];
            if (dl) {
                memcpy(c, path, dl);
                c[dl++] = '/';
            }
            memcpy(c+dl, file, fl+1);
            char b[PM];
            const char*h;
            if (tr(AT_FDCWD, c, b, 1, &h) == 0) {
                long r = SC3(N_faccessat, AT_FDCWD, h, 1);
                if (r == 0) return do_spawn(pid, c, fa, at, a, e);
                if (r == -EACCES) eacces = 1;
            }
        }
        if (!*q) break;
        path = q+1;
    }
    return eacces ? EACCES : ENOENT;
}
EXP int system(const char*cmd) {
    if (!cmd) return 1;
    char*av[4] = {"sh", "-c", (char*) cmd, NULL};
    int pid;
    void*oi = signal(2, (void*) 1), *oq = signal(3, (void*) 1);
    char sat[512]__attribute__((aligned(16)));
    char sss[128]__attribute__((aligned(16)));
    posix_spawnattr_init(sat);
    sigemptyset(sss);
    sigaddset(sss, 2);
    sigaddset(sss, 3);
    posix_spawnattr_setsigdefault(sat, sss);
    posix_spawnattr_setflags(sat, 0x04);
    int r = do_spawn(&pid, "/bin/sh", NULL, sat, av, environ);
    posix_spawnattr_destroy(sat);
    int st = -1;
    if (r == 0) {
        while (waitpid(pid, &st, 0)<0 && geterr() == EINTR);
    } else {
        st = 127<<8;
    }
    signal(2, oi);
    signal(3, oq);
    return st;
}
static struct {
    void*f;
    int pid;
} g_po[64];
EXP void*popen(const char*cmd, const char*mode) {
    int rd = (mode && mode[0] == 'r');
    int fds[2];
    if (!mode || (mode[0] != 'r' && mode[0] != 'w')) {
        fail(EINVAL);
        return NULL;
    }
    if (pipe2(fds, O_CLOEXEC)<0) return NULL;
    char fa[512]__attribute__((aligned(16)));
    posix_spawn_file_actions_init(fa);
    if (rd) posix_spawn_file_actions_adddup2(fa, fds[1], 1);
    else posix_spawn_file_actions_adddup2(fa, fds[0], 0);
    char*av[4] = {"sh", "-c", (char*) cmd, NULL};
    int pid;
    int r = do_spawn(&pid, "/bin/sh", fa, NULL, av, environ);
    posix_spawn_file_actions_destroy(fa);
    int mine = rd ? fds[0] : fds[1], theirs = rd ? fds[1] : fds[0];
    sc(N_close, theirs, 0, 0, 0, 0, 0);
    if (r) {
        sc(N_close, mine, 0, 0, 0, 0, 0);
        fail(r);
        return NULL;
    }
    void*f = fdopen(mine, rd ? "r" : "w");
    if (!f) {
        sc(N_close, mine, 0, 0, 0, 0, 0);
        return NULL;
    }
    for (int i = 0; i<64; i++) {
        if (!g_po[i].f) {
            g_po[i].f = f;
            g_po[i].pid = pid;
            break;
        }
    }
    return f;
}
EXP int pclose(void*f) {
    for (int i = 0; i<64; i++) {
        if (g_po[i].f == f) {
            int pid = g_po[i].pid, st = -1;
            g_po[i].f = NULL;
            fclose(f);
            while (waitpid(pid, &st, 0)<0 && geterr() == EINTR);
            return st;
        }
    }
    static int(*real)(void*);
    if (!real) real = nextsym("pclose");
    return real(f);
}
static int parse_v4(const char*s, size_t n, u8 out[4]) {
    size_t i = 0;
    for (int part = 0; part<4; part++) {
        unsigned v = 0, d = 0;
        while (i<n && s[i] >= '0' && s[i] <= '9') {
            v = v*10+(unsigned)(s[i++]-'0');
            d++;
            if (v>255) return 0;
        }
        if (!d) return 0;
        out[part] = (u8) v;
        if (part<3) {
            if (i >= n || s[i] != '.') return 0;
            i++;
        }
    }
    return i == n;
}
/* IPv6 literal (RFC 4291 text form, incl. "::" and an embedded dotted quad).
 * Zone ids ("%eth0") are not accepted. */
static int parse_v6(const char*s, size_t n, u8 out[16]) {
    u16 w[8];
    int nw = 0, gap = -1;
    size_t i = 0;
    if (!n || n>45) return 0;
    if (n >= 2 && s[0] == ':' && s[1] == ':') {
        gap = 0;
        i = 2;
        if (i == n) {
            memset(out, 0, 16);
            return 1;
        }
    } else if (s[0] == ':') return 0;
    while (i<n) {
        size_t st = i;
        unsigned v = 0;
        int d = 0;
        while (i<n && ((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') || (s[i] >= 'A' && s[i] <= 'F'))) {
            char c = s[i++];
            v = v*16+(unsigned)(c <= '9' ? c-'0' : (c|32)-'a'+10);
            if (++d>4) return 0;
        }
        if (i<n && s[i] == '.') {
            u8 q[4];
            if (nw>6 || !parse_v4(s+st, n-st, q)) return 0;
            w[nw++] = (u16)((q[0]<<8)|q[1]);
            w[nw++] = (u16)((q[2]<<8)|q[3]);
            i = n;
            break;
        }
        if (!d || nw >= 8) return 0;
        w[nw++] = (u16) v;
        if (i == n) break;
        if (s[i] != ':') return 0;
        i++;
        if (i<n && s[i] == ':') {
            if (gap >= 0) return 0;
            gap = nw;
            i++;
            if (i == n) break;
        } else if (i == n) return 0;
    }
    if (gap < 0 ? nw != 8 : nw >= 8) return 0;
    int z = 8-nw;
    int o = 0;
    for (int k = 0; k<8; k++) {
        u16 v;
        if (gap >= 0 && k >= gap && k<gap+z) v = 0;
        else v = w[o++];
        out[2*k] = (u8)(v>>8);
        out[2*k+1] = (u8) v;
    }
    return 1;
}
static void fmt_v4(const u8 ip[4], char*o) {
    for (int i = 0; i<4; i++) {
        unsigned v = ip[i];
        if (v >= 100)*o++ = (char)('0'+v/100);
        if (v >= 10)*o++ = (char)('0'+(v/10)%10);
        *o++ = (char)('0'+v%10);
        if (i<3)*o++ = '.';
    }
    *o = 0;
}
static int hosts_lookup(const char*name, u8 ip[4]) {
    long n;
    char*buf = read_rel_all("/etc/hosts", &n);
    if (!buf) return 0;
    if (n <= 0) {
        free(buf);
        return 0;
    }
    size_t nl = strlen(name);
    char*p = buf;
    int found = 0;
    while (*p) {
        char*eol = p;
        while (*eol && *eol != '\n') eol++;
        char*c = p;
        while (c<eol && (*c == ' ' || *c == '\t')) c++;
        if (c<eol && *c != '#') {
            char*t = c;
            while (t<eol && *t != ' ' && *t != '\t') t++;
            u8 a[4];
            if (parse_v4(c, (size_t)(t-c), a)) {
                while (t<eol && *t != '#') {
                    while (t<eol && (*t == ' ' || *t == '\t')) t++;
                    char*w = t;
                    while (t<eol && *t != ' ' && *t != '\t' && *t != '#') t++;
                    if ((size_t)(t-w) == nl && !memcmp(w, name, nl)) {
                        memcpy(ip, a, 4);
                        found = 1;
                        break;
                    }
                }
            }
        }
        if (found) break;
        p = *eol ? eol+1 : eol;
    }
    free(buf);
    return found;
}
/* Ask one server (IPv4 or IPv6) for an A record. Returns 1 and fills ip on
 * success, 0 otherwise. */
static int dns_ask(int fam, const u8*saddr, const u8*q, size_t k, const u8 id[2], u8 ip[4]) {
    int s = socket(fam, 2, 0);
    if (s < 0) return 0;
    int found = 0;
    struct {
        u16 fam;
        u16 port;
        u32 flow;
        u8 addr[16];
        u32 scope;
    } sa6;
    struct {
        u16 fam;
        u16 port;
        u8 addr[4];
        u8 z[8];
    } sa4;
    const void*sa;
    unsigned sl;
    u16 port = (u16)((53>>8)|((53&0xff)<<8));
    if (fam == 10) {
        memset(&sa6, 0, sizeof sa6);
        sa6.fam = 10;
        sa6.port = port;
        memcpy(sa6.addr, saddr, 16);
        sa = &sa6;
        sl = sizeof sa6;
    } else {
        memset(&sa4, 0, sizeof sa4);
        sa4.fam = 2;
        sa4.port = port;
        memcpy(sa4.addr, saddr, 4);
        sa = &sa4;
        sl = sizeof sa4;
    }
    long tv[2] = {2, 0};
    setsockopt(s, 1, 20, tv, sizeof tv);
    if (connect(s, sa, sl) == 0) {
        for (int tries = 0; tries<2 && !found; tries++) {
            if (send(s, q, k, 0)<0) break;
            u8 r[1500];
            long rn = recv(s, r, sizeof r, 0);
            if (rn<12 || r[0] != id[0] || r[1] != id[1]) continue;
            if ((r[3]&0x0f) != 0) break;
            int an = (r[6]<<8)|r[7];
            long o = 12;
            int qd = (r[4]<<8)|r[5];
            for (int i = 0; i<qd && o<rn; i++) {
                while (o<rn && r[o]) {
                    if ((r[o]&0xc0) == 0xc0) {
                        o++;
                        break;
                    }
                    o += r[o]+1;
                }
                o += 1+4;
            }
            for (int i = 0; i<an && o<rn; i++) {
                while (o<rn && r[o]) {
                    if ((r[o]&0xc0) == 0xc0) {
                        o++;
                        break;
                    }
                    o += r[o]+1;
                }
                o++;
                if (o+10>rn) break;
                int ty = (r[o]<<8)|r[o+1];
                int rl = (r[o+8]<<8)|r[o+9];
                o += 10;
                if (ty == 1 && rl == 4 && o+4 <= rn) {
                    memcpy(ip, r+o, 4);
                    found = 1;
                    break;
                }
                o += rl;
            }
            break;
        }
    }
    sc(N_close, s, 0, 0, 0, 0, 0);
    return found;
}
static int dns_query(const char*name, u8 ip[4]) {
    u8 q[300];
    size_t nl = strlen(name);
    if (!nl || nl>253) return 0;
    u8 id[2];
    sc(N_getrandom, (long) id, 2, 0, 0, 0, 0);
    size_t k = 0;
    q[k++] = id[0];
    q[k++] = id[1];
    q[k++] = 0x01;
    q[k++] = 0x00;
    q[k++] = 0;
    q[k++] = 1;
    for (int i = 0; i<6; i++) q[k++] = 0;
    const char*p = name;
    while (*p) {
        const char*e = p;
        while (*e && *e != '.') e++;
        size_t l = (size_t)(e-p);
        if (!l || l>63) return 0;
        q[k++] = (u8) l;
        memcpy(q+k, p, l);
        k += l;
        p = *e ? e+1 : e;
    }
    q[k++] = 0;
    q[k++] = 0;
    q[k++] = 1;
    q[k++] = 0;
    q[k++] = 1;
    /* g_dns: comma-separated IPv4 / IPv6 literals ("[addr]" is accepted too) */
    const char*sv = g_dns;
    while (*sv) {
        const char*e = sv;
        while (*e && *e != ',') e++;
        const char*a = sv;
        size_t al = (size_t)(e-sv);
        if (al >= 2 && a[0] == '[' && a[al-1] == ']') {
            a++;
            al -= 2;
        }
        u8 sip[16];
        if (parse_v4(a, al, sip)) {
            if (dns_ask(2, sip, q, k, id, ip)) return 1;
        } else if (parse_v6(a, al, sip)) {
            if (dns_ask(10, sip, q, k, id, ip)) return 1;
        }
        sv = *e ? e+1 : e;
    }
    return 0;
}
static int resolve_name(const char*name, char*ipstr) {
    u8 ip[4];
    if (hosts_lookup(name, ip) || dns_query(name, ip)) {
        fmt_v4(ip, ipstr);
        return 1;
    }
    return 0;
}
static int is_numeric(const char*n) {
    u8 t[4];
    if (parse_v4(n, strlen(n), t)) return 1;
    for (const char*c = n; *c; c++) if (*c == ':') return 1;
    return 0;
}
struct ai_hints {
    int flags, family, socktype, protocol;
};
#define AI_NUMERICHOST 4
EXP int getaddrinfo(const char*node, const char*service, const void*hints, void**res) {
    static int(*real)(const char*, const char*, const void*, void**);
    if (!real) real = nextsym("getaddrinfo");
    int r = real(node, service, hints, res);
    if (r == 0 || !g_rl || !node || is_numeric(node)) return r;
    const struct ai_hints*h = hints;
    if (h && (h->flags&AI_NUMERICHOST)) return r;
    if (h && h->family == 10) return r;
    char ip[32];
    if (!resolve_name(node, ip)) return r;
    struct ai_hints nh = {0, 0, 0, 0};
    if (h) nh = *h;
    nh.flags |= AI_NUMERICHOST;
    char hb[64];
    memset(hb, 0, sizeof hb);
    memcpy(hb, &nh, sizeof nh);
    return real(ip, service, hb, res);
}
EXP void*gethostbyname(const char*name) {
    static void*(*real)(const char*);
    if (!real) real = nextsym("gethostbyname");
    void*r = real(name);
    if (r || !g_rl || !name || is_numeric(name)) return r;
    char ip[32];
    if (resolve_name(name, ip)) return real(ip);
    return r;
}
