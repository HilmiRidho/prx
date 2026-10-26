/* prxsup - optional ptrace supervisor for prx.   SPDX-License-Identifier: Apache-2.0
 *
 * The LD_PRELOAD shim cannot translate paths for programs that never go
 * through the guest's libc: static binaries (Go, busybox-static, ...) and raw
 * syscalls.  With `prx login --ptrace` the shim hands exactly those programs
 * to this helper:
 *
 *   prxsup run -- HOSTPATH ARGV0 [ARGS...]
 *
 * `run` keeps the caller's pid.  It forks a tracer (a double fork, so it is
 * nobody's child), lets the tracer PTRACE_SEIZE it, installs a seccomp-bpf
 * filter that stops only the path-taking syscalls (SECCOMP_RET_TRACE) and then
 * execs the program.  Everything else runs at native speed, and processes that
 * never run static code are never traced at all.
 *
 * The tracer follows the whole process tree below that program and rewrites
 * the path arguments of the trapped syscalls exactly like the shim does
 * (same root, same binds, same symlink rules, see x_tr()).  A traced task is
 * either
 *   - "xlate":   guest code without the shim (a static program): its paths are
 *                guest paths and are translated here; or
 *   - "managed": shim-managed or native host code: its paths are already host
 *                paths, so the stop is resumed untouched.
 * execve decides which one a task becomes.  A static ELF stays "xlate"; any
 * other guest program (dynamic ELF, script, ...) is handed back to the shim by
 * running the guest shell under the loader with PRX_DELEGATE=1.  The shim's
 * constructor then performs the real execve() itself, so shebangs, the
 * loader, credentials and everything else behave exactly as without ptrace.
 *
 * Scratch memory for rewritten arguments is a private anonymous mapping that
 * the tracer injects into the tracee (never the stack: Go stacks are tiny).
 *
 * Limits: x86_64 and aarch64 hosts, native-architecture rootfs only (no
 * qemu-user), not a security boundary.  Static programs see no fake uid/gid,
 * no /proc fakes and no AF_UNIX path translation.
 */
#define _GNU_SOURCE
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

#if defined(__x86_64__)
#include <sys/user.h>
#define SUP_ARCH_OK 1
#define SUP_AUDIT_ARCH AUDIT_ARCH_X86_64
#define SUP_EM EM_X86_64
#elif defined(__aarch64__)
#include <sys/uio.h>
#include <asm/ptrace.h>
#define SUP_ARCH_OK 1
#define SUP_AUDIT_ARCH AUDIT_ARCH_AARCH64
#define SUP_EM EM_AARCH64
#endif

#define SUP_VERSION "1"

/* ptrace constants that older libcs only provide as enum members (or not at
   all); the values are part of the stable kernel ABI. */
#define SUP_SEIZE 0x4206
#define SUP_LISTEN 0x4208
#define SUP_O_TRACESYSGOOD 0x1
#define SUP_O_TRACEFORK 0x2
#define SUP_O_TRACEVFORK 0x4
#define SUP_O_TRACECLONE 0x8
#define SUP_O_TRACEEXEC 0x10
#define SUP_O_TRACESECCOMP 0x80
#define SUP_O_EXITKILL 0x100000
#define SUP_EV_FORK 1
#define SUP_EV_VFORK 2
#define SUP_EV_CLONE 3
#define SUP_EV_EXEC 4
#define SUP_EV_SECCOMP 7
#define SUP_EV_STOP 128

#define PM 4096
#define MAXB 64
#define BPM 1024
#define SLOT_SZ 8192            /* per task: two PATH_MAX strings */
#define NSLOTS 512
#define ARENA_SZ (256 * 1024)   /* argv/envp rewriting for execve */
#define REGION_SZ ((size_t) NSLOTS * SLOT_SZ + ARENA_SZ)
#define MAXPTRS 32768           /* argv/envp entries read from a tracee */
#define NO_REGION ((unsigned long) -1)

#ifndef AT_EMPTY_PATH
#define AT_EMPTY_PATH 0x1000
#endif
#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x100
#endif
#ifndef AT_SYMLINK_FOLLOW
#define AT_SYMLINK_FOLLOW 0x400
#endif

static int g_dbg;
static int g_probe;

static void lg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void lg(const char *fmt, ...)
{
    char b[1024];
    va_list ap;
    int n;
    if (!g_dbg) return;
    n = snprintf(b, sizeof b, "prxsup[%d]: ", (int) getpid());
    va_start(ap, fmt);
    n += vsnprintf(b + n, sizeof b - (size_t) n - 1, fmt, ap);
    va_end(ap);
    if (n > (int) sizeof b - 2) n = (int) sizeof b - 2;
    b[n++] = '\n';
    if (write(2, b, (size_t) n) < 0) { /* nothing sensible to do */ }
}

static void wr(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void wr(const char *fmt, ...)
{
    char b[1024];
    va_list ap;
    int n;
    n = snprintf(b, sizeof b, "prxsup: ");
    va_start(ap, fmt);
    n += vsnprintf(b + n, sizeof b - (size_t) n - 1, fmt, ap);
    va_end(ap);
    if (n > (int) sizeof b - 2) n = (int) sizeof b - 2;
    b[n++] = '\n';
    if (write(2, b, (size_t) n) < 0) { /* nothing sensible to do */ }
}

/* ======================================================================== *
 *  Path translation: a port of tr()/canon()/mkabs() from prx.c.            *
 *  Keep the two in sync; tests/xlate_cmp.sh compares them.                  *
 * ======================================================================== */

static char g_root[PM];
static size_t g_rl;
static char g_bind[MAXB][BPM], g_bhost[MAXB][BPM];
static size_t g_bindlen[MAXB], g_bhlen[MAXB];
static int g_bid[MAXB];
static int g_nb;
static char g_ld[PM], g_sh[PM], g_shim[PM];
static const char *g_libpath = "";

static void addbind(const char *s, size_t n)
{
    size_t eq = n, i, gn, hs, hn;
    const char *hp;
    for (i = 0; i < n; i++) {
        if (s[i] == '=') {
            eq = i;
            break;
        }
    }
    gn = eq;
    hs = eq < n ? eq + 1 : 0;
    hn = eq < n ? n - eq - 1 : n;
    hp = eq < n ? s + hs : s;
    if (!gn || g_nb >= MAXB || gn >= BPM || hn >= BPM || s[0] != '/' || hp[0] != '/') return;
    memcpy(g_bind[g_nb], s, gn);
    g_bind[g_nb][gn] = 0;
    while (gn > 1 && g_bind[g_nb][gn - 1] == '/') g_bind[g_nb][--gn] = 0;
    memcpy(g_bhost[g_nb], hp, hn);
    g_bhost[g_nb][hn] = 0;
    while (hn > 1 && g_bhost[g_nb][hn - 1] == '/') g_bhost[g_nb][--hn] = 0;
    g_bindlen[g_nb] = gn;
    g_bhlen[g_nb] = hn;
    g_bid[g_nb] = (gn == hn && !memcmp(g_bind[g_nb], g_bhost[g_nb], gn));
    g_nb++;
}

static void cpyenv(char *dst, size_t cap, const char *name)
{
    const char *v = getenv(name);
    size_t n = v ? strlen(v) : 0;
    if (n >= cap) n = cap - 1;
    if (n) memcpy(dst, v, n);
    dst[n] = 0;
}

/* Reads the same PRX_* variables the shim reads. Returns 0 when there is no
   rootfs configured (nothing to translate). */
static int cfg_load(void)
{
    static const char *const builtin[] = {
        "/proc", "/sys", "/dev/urandom", "/dev/random", "/dev/zero", "/dev/null", "/dev/full",
        "/dev/pts", "/dev/ptmx", "/dev/fd", "/dev/stdin", "/dev/stdout", "/dev/stderr", "/dev/tty"
    };
    const char *p, *q, *e = getenv("PRX_TRACE");
    size_t i;
    g_dbg = e && e[0] == '1';
    cpyenv(g_root, sizeof g_root, "PRX_ROOTFS");
    cpyenv(g_ld, sizeof g_ld, "PRX_LD");
    cpyenv(g_sh, sizeof g_sh, "PRX_SH");
    cpyenv(g_shim, sizeof g_shim, "PRX_SHIM");
    e = getenv("PRX_LIBPATH");
    g_libpath = e ? e : "";
    g_rl = strlen(g_root);
    while (g_rl > 1 && g_root[g_rl - 1] == '/') g_root[--g_rl] = 0;
    if (g_rl == 1) g_rl = 0;
    if (!g_rl) return 0;
    g_nb = 0;
    for (i = 0; i < sizeof builtin / sizeof builtin[0]; i++) addbind(builtin[i], strlen(builtin[i]));
    p = getenv("PRX_BIND");
    while (p && *p) {
        q = p;
        while (*q && *q != ':') q++;
        addbind(p, (size_t) (q - p));
        p = *q ? q + 1 : q;
    }
    return 1;
}

static int in_bind(const char *g)
{
    int best = 0, i;
    size_t best_len = 0;
    for (i = 0; i < g_nb; i++) {
        size_t l = g_bindlen[i];
        if (l > best_len && strncmp(g, g_bind[i], l) == 0 && (g[l] == 0 || g[l] == '/')) {
            best = i + 1;
            best_len = l;
        }
    }
    return best;
}

static int hasroot(const char *p)
{
    return g_rl && strncmp(p, g_root, g_rl) == 0 && (p[g_rl] == 0 || p[g_rl] == '/');
}

static int bind_to_host(int i, const char *res, char *buf)
{
    const char *rest = res + g_bindlen[i];
    size_t hl = g_bhlen[i], rl = strlen(rest);
    if (hl + rl >= PM) return -ENAMETOOLONG;
    memcpy(buf, g_bhost[i], hl);
    memcpy(buf + hl, rest, rl + 1);
    return 0;
}

static const char *rev_bind(const char *host, char *out)
{
    int best = -1, i;
    size_t best_hl = 0;
    for (i = 0; i < g_nb; i++) {
        size_t hl;
        if (g_bid[i]) continue;
        hl = g_bhlen[i];
        if (hl > best_hl && strncmp(host, g_bhost[i], hl) == 0 && (host[hl] == 0 || host[hl] == '/')) {
            best = i;
            best_hl = hl;
        }
    }
    if (best >= 0) {
        size_t gl = g_bindlen[best], rl = strlen(host + best_hl);
        if (gl + rl >= PM) return NULL;
        memcpy(out, g_bind[best], gl);
        memcpy(out + gl, host + best_hl, rl + 1);
        return out;
    }
    return NULL;
}

static int has_dotdot(const char *s)
{
    while (*s) {
        const char *t;
        while (*s == '/') s++;
        t = s;
        while (*s && *s != '/') s++;
        if (s - t == 2 && t[0] == '.' && t[1] == '.') return 1;
    }
    return 0;
}

/* Directory a relative path is resolved against: the cwd (AT_FDCWD) or an open
   directory fd of the tracee, as a host path. */
static int fd_base(pid_t pid, int dfd, char *base)
{
    char l[64];
    ssize_t n;
    if (dfd == AT_FDCWD) snprintf(l, sizeof l, "/proc/%d/cwd", (int) pid);
    else snprintf(l, sizeof l, "/proc/%d/fd/%d", (int) pid, dfd);
    n = readlink(l, base, PM - 1);
    if (n > 0) base[n] = 0;
    return (int) n;
}

/* 1: use the path as it is; 0: g holds the absolute guest path; <0: -errno */
static int x_mkabs(pid_t pid, int dfd, const char *in, char *g)
{
    size_t il = strlen(in), bl;
    char base[PM], rb[PM];
    const char *b;
    int ib;
    if (in[0] == '/') {
        int bi;
        if (hasroot(in)) return 1;
        bi = in_bind(in);
        if (bi && g_bid[bi - 1] && !has_dotdot(in)) return 1;
        if (il >= PM) return -ENAMETOOLONG;
        memcpy(g, in, il + 1);
        return 0;
    }
    if (fd_base(pid, dfd, base) <= 0) return 1;
    if (hasroot(base)) {
        b = base + g_rl;
        if (!*b) b = "/";
    } else if ((ib = in_bind(base)) && g_bid[ib - 1]) {
        if (!has_dotdot(in)) return 1;
        b = base;
    } else if (!(b = rev_bind(base, rb))) {
        return 1;
    }
    bl = strlen(b);
    if (bl + il + 2 >= PM) return -ENAMETOOLONG;
    memcpy(g, b, bl);
    if (g[bl - 1] != '/') g[bl++] = '/';
    memcpy(g + bl, in, il + 1);
    return 0;
}

static int x_canon(const char *gin, char *res, int follow, int *bind)
{
    char rest[PM], lnk[PM], host[PM];
    size_t rl = 1, n = strlen(gin);
    int links = 0;
    char *p;
    res[0] = '/';
    res[1] = 0;
    *bind = 0;
    memcpy(rest, gin, n + 1);
    p = rest;
    for (;;) {
        char *e, *q;
        size_t cl, save;
        int last, has_trailing, bi;
        while (*p == '/') p++;
        if (!*p) break;
        e = p;
        while (*e && *e != '/') e++;
        cl = (size_t) (e - p);
        q = e;
        while (*q == '/') q++;
        last = (*q == 0);
        has_trailing = (*e == '/');
        if (cl == 1 && p[0] == '.') {
            p = e;
            continue;
        }
        if (cl == 2 && p[0] == '.' && p[1] == '.') {
            if (rl > 1) {
                size_t i = rl - 1;
                while (res[i] != '/') i--;
                rl = i ? i : 1;
                res[rl] = 0;
            }
            p = e;
            continue;
        }
        if (g_rl + rl + 1 + cl >= PM) return -ENAMETOOLONG;
        save = rl;
        if (rl > 1) res[rl++] = '/';
        memcpy(res + rl, p, cl);
        rl += cl;
        res[rl] = 0;
        p = e;
        bi = in_bind(res);
        if (bi && g_bid[bi - 1]) {
            size_t tl;
            if (has_dotdot(p)) continue;
            tl = strlen(p);
            if (rl + tl >= PM) return -ENAMETOOLONG;
            memcpy(res + rl, p, tl + 1);
            *bind = bi;
            return 0;
        }
        if (!last || follow || has_trailing) {
            ssize_t r;
            if (bi) {
                int err = bind_to_host(bi - 1, res, host);
                if (err < 0) return err;
            } else {
                memcpy(host, g_root, g_rl);
                memcpy(host + g_rl, res, rl + 1);
            }
            r = readlink(host, lnk, PM - 1);
            if (r > 0) {
                size_t tl, ll;
                if (++links > 40) return -ELOOP;
                lnk[r] = 0;
                rl = save;
                res[rl] = 0;
                if (lnk[0] == '/') {
                    rl = 1;
                    res[1] = 0;
                }
                tl = strlen(p);
                ll = (size_t) r;
                if (ll + 1 + tl >= PM) return -ENAMETOOLONG;
                memmove(rest + ll + 1, p, tl + 1);
                memcpy(rest, lnk, ll);
                rest[ll] = '/';
                p = rest;
            }
        }
    }
    *bind = in_bind(res);
    return 0;
}

/* Translate `in` (a guest path of task `pid`) into a host path. On success *out
   is `in` itself when nothing has to change, or `buf`. */
static int x_tr(pid_t pid, int dfd, const char *in, char *buf, int follow, const char **out)
{
    char g[PM], res[PM];
    int bind, m;
    size_t rl;
    *out = in;
    if (!g_rl || !in || !*in) return 0;
    m = x_mkabs(pid, dfd, in, g);
    if (m > 0) return 0;
    if (m < 0) return m;
    m = x_canon(g, res, follow, &bind);
    if (m < 0) return m;
    rl = strlen(res);
    if (bind) {
        m = bind_to_host(bind - 1, res, buf);
        if (m < 0) return m;
    } else {
        if (g_rl + rl >= PM) return -ENAMETOOLONG;
        memcpy(buf, g_root, g_rl);
        if (rl > 1) memcpy(buf + g_rl, res, rl + 1);
        else buf[g_rl] = 0;
    }
    *out = buf;
    return 0;
}

/* host path -> guest path for results (getcwd, readlink): the inverse of x_tr */
static const char *x_back(const char *host, char *rb)
{
    if (hasroot(host)) {
        const char *s = host + g_rl;
        return *s ? s : "/";
    }
    if (g_rl) {
        const char *r = rev_bind(host, rb);
        if (r) return r;
    }
    return host;
}

/* ======================================================================== *
 *  Reading and writing tracee memory (/proc/PID/mem: no extra syscalls that *
 *  a restrictive host seccomp policy might reject).                         *
 * ======================================================================== */

static int g_mfd = -1;
static pid_t g_mpid;

static void mem_done(void)
{
    if (g_mfd >= 0) close(g_mfd);
    g_mfd = -1;
    g_mpid = 0;
}

static int mem_fd(pid_t pid)
{
    char p[48];
    if (g_mfd >= 0 && g_mpid == pid) return g_mfd;
    mem_done();
    snprintf(p, sizeof p, "/proc/%d/mem", (int) pid);
    g_mfd = open(p, O_RDWR | O_CLOEXEC);
    if (g_mfd < 0) return -1;
    g_mpid = pid;
    return g_mfd;
}

static int mem_read(pid_t pid, unsigned long addr, void *buf, size_t n)
{
    int fd = mem_fd(pid);
    ssize_t k;
    if (fd < 0) return -1;
    k = pread(fd, buf, n, (off_t) addr);
    return k == (ssize_t) n ? 0 : -1;
}

static int mem_write(pid_t pid, unsigned long addr, const void *buf, size_t n)
{
    int fd = mem_fd(pid);
    ssize_t k;
    if (fd < 0) return -1;
    k = pwrite(fd, buf, n, (off_t) addr);
    return k == (ssize_t) n ? 0 : -1;
}

/* Copy the NUL-terminated string at addr. Returns its length, -EFAULT or
   -ENAMETOOLONG. Reads never cross a page boundary in one go. */
static ssize_t read_str(pid_t pid, unsigned long addr, char *buf, size_t cap)
{
    size_t got = 0;
    int fd = mem_fd(pid);
    if (fd < 0) return -EFAULT;
    while (got < cap) {
        size_t chunk = 4096 - ((addr + got) & 4095), i;
        ssize_t k;
        if (chunk > cap - got) chunk = cap - got;
        k = pread(fd, buf + got, chunk, (off_t) (addr + got));
        if (k <= 0) return -EFAULT;
        for (i = 0; i < (size_t) k; i++) if (buf[got + i] == 0) return (ssize_t) (got + i);
        got += (size_t) k;
    }
    return -ENAMETOOLONG;
}

/* Read a NULL-terminated pointer array (argv/envp). Returns the number of
   entries (without the terminator), or -1. */
static long read_ptrs(pid_t pid, unsigned long addr, unsigned long *out, long max)
{
    long n = 0;
    while (n < max) {
        unsigned long tmp[128];
        size_t want = sizeof tmp, i;
        size_t room = 4096 - ((addr + (unsigned long) n * 8) & 4095);
        int fd = mem_fd(pid);
        ssize_t k;
        if (fd < 0) return -1;
        if (want > room) want = room;
        k = pread(fd, tmp, want, (off_t) (addr + (unsigned long) n * 8));
        if (k < 8) return -1;
        for (i = 0; i < (size_t) k / 8 && n < max; i++) {
            if (!tmp[i]) return n;
            out[n++] = tmp[i];
        }
    }
    return -1;
}

/* ======================================================================== *
 *  Registers: the only architecture-specific part (besides the BPF arch).   *
 * ======================================================================== */

#if defined(__x86_64__)
typedef struct user_regs_struct regs_t;
static int arch_getregs(pid_t p, regs_t *r) { return (int) ptrace(PTRACE_GETREGS, p, 0, r); }
static int arch_setregs(pid_t p, regs_t *r) { return (int) ptrace(PTRACE_SETREGS, p, 0, r); }
static long reg_nr(const regs_t *r) { return (long) r->orig_rax; }
static unsigned long reg_arg(const regs_t *r, int i)
{
    switch (i) {
    case 0: return r->rdi;
    case 1: return r->rsi;
    case 2: return r->rdx;
    case 3: return r->r10;
    case 4: return r->r8;
    default: return r->r9;
    }
}
static void reg_set_arg(regs_t *r, int i, unsigned long v)
{
    switch (i) {
    case 0: r->rdi = v; break;
    case 1: r->rsi = v; break;
    case 2: r->rdx = v; break;
    case 3: r->r10 = v; break;
    case 4: r->r8 = v; break;
    default: r->r9 = v; break;
    }
}
static long reg_ret(const regs_t *r) { return (long) r->rax; }
static void reg_set_ret(regs_t *r, long v) { r->rax = (unsigned long) v; }
/* The syscall to run is orig_rax; it takes effect with the next setregs. */
static int arch_set_nr(pid_t p, regs_t *r, long nr) { (void) p; r->orig_rax = (unsigned long) nr; return 0; }
/* Entry stops report rax == -ENOSYS, exit stops the result. */
static int reg_is_entry(const regs_t *r) { return (long) r->rax == -ENOSYS; }
/* Re-execute the syscall instruction (2 bytes) with number nr. */
static void reg_rewind(regs_t *r, long nr)
{
    r->rip -= 2;
    r->rax = (unsigned long) nr;
    r->orig_rax = (unsigned long) nr;
}
#elif defined(__aarch64__)
#ifndef NT_PRSTATUS
#define NT_PRSTATUS 1
#endif
#ifndef NT_ARM_SYSTEM_CALL
#define NT_ARM_SYSTEM_CALL 0x404
#endif
typedef struct user_pt_regs regs_t;
static int arch_getregs(pid_t p, regs_t *r)
{
    struct iovec iov = { r, sizeof *r };
    return (int) ptrace(PTRACE_GETREGSET, p, (void *) NT_PRSTATUS, &iov);
}
static int arch_setregs(pid_t p, regs_t *r)
{
    struct iovec iov = { r, sizeof *r };
    return (int) ptrace(PTRACE_SETREGSET, p, (void *) NT_PRSTATUS, &iov);
}
static long reg_nr(const regs_t *r) { return (long) r->regs[8]; }
static unsigned long reg_arg(const regs_t *r, int i) { return r->regs[i]; }
static void reg_set_arg(regs_t *r, int i, unsigned long v) { r->regs[i] = v; }
static long reg_ret(const regs_t *r) { return (long) r->regs[0]; }
static void reg_set_ret(regs_t *r, long v) { r->regs[0] = (unsigned long) v; }
/* aarch64 only honours the syscall number written through NT_ARM_SYSTEM_CALL. */
static int arch_set_nr(pid_t p, regs_t *r, long nr)
{
    int v = (int) nr;
    struct iovec iov = { &v, sizeof v };
    (void) r;
    return (int) ptrace(PTRACE_SETREGSET, p, (void *) NT_ARM_SYSTEM_CALL, &iov);
}
/* The kernel stores 0 (entry) / 1 (exit) in x7 while a syscall stop is reported. */
static int reg_is_entry(const regs_t *r) { return r->regs[7] == 0; }
static void reg_rewind(regs_t *r, long nr)
{
    r->pc -= 4;
    r->regs[8] = (unsigned long) nr;
}
#endif

/* The launcher gets LD_PRELOAD and LD_LIBRARY_PATH under another name so the
   host loader does not load the shim or guest libraries into this host
   program; hand them back to the program being started. */
static void restore_env(void)
{
    static const char *const v[] = { "LD_PRELOAD", "LD_LIBRARY_PATH" };
    char n[64];
    size_t i;
    for (i = 0; i < 2; i++) {
        const char *s;
        snprintf(n, sizeof n, "PRX_SUP_%s", v[i]);
        s = getenv(n);
        if (s) {
            setenv(v[i], s, 1);
            unsetenv(n);
        }
    }
}

static int plain_exec(const char *path, char **argv)
{
    syscall(SYS_execve, path, argv, environ);
    wr("cannot run %s: %s", path, strerror(errno));
    return errno == ENOENT ? 127 : 126;
}

#ifdef SUP_ARCH_OK

/* ======================================================================== *
 *  ELF classification                                                       *
 * ======================================================================== */

enum { K_STATIC = 0, K_OTHER = 1 };

/* K_STATIC: a native 64-bit ELF with no interpreter and no DT_NEEDED/DT_SONAME,
   i.e. code that will never load the shim. The dynamic loader itself has a
   SONAME (and no PT_INTERP), so it is K_OTHER. Anything unreadable, a script or
   a foreign binary is K_OTHER, too: the shim knows what to do with those. */
static int exec_kind(const char *host)
{
    unsigned char hb[sizeof(Elf64_Ehdr)];
    Elf64_Ehdr *eh = (Elf64_Ehdr *) hb;
    Elf64_Phdr ph[64];
    int fd, kind = K_OTHER;
    unsigned i, n;
    fd = open(host, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return K_OTHER;
    if (pread(fd, hb, sizeof hb, 0) != (ssize_t) sizeof hb) goto out;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) || eh->e_ident[EI_CLASS] != ELFCLASS64
        || eh->e_ident[EI_DATA] != ELFDATA2LSB || eh->e_machine != SUP_EM
        || (eh->e_type != ET_EXEC && eh->e_type != ET_DYN)
        || eh->e_phentsize != sizeof(Elf64_Phdr) || !eh->e_phnum || eh->e_phnum > 64)
        goto out;
    n = eh->e_phnum;
    if (pread(fd, ph, n * sizeof ph[0], (off_t) eh->e_phoff) != (ssize_t) (n * sizeof ph[0])) goto out;
    for (i = 0; i < n; i++) if (ph[i].p_type == PT_INTERP) goto out;
    kind = K_STATIC;
    for (i = 0; i < n; i++) {
        Elf64_Dyn d[512];
        ssize_t k;
        size_t j, cnt;
        if (ph[i].p_type != PT_DYNAMIC) continue;
        k = pread(fd, d, sizeof d, (off_t) ph[i].p_offset);
        if (k < 0) k = 0;
        cnt = (size_t) k / sizeof d[0];
        if (cnt > ph[i].p_filesz / sizeof d[0]) cnt = ph[i].p_filesz / sizeof d[0];
        for (j = 0; j < cnt && d[j].d_tag != DT_NULL; j++) {
            if (d[j].d_tag == DT_NEEDED || d[j].d_tag == DT_SONAME) {
                kind = K_OTHER;
                break;
            }
        }
    }
out:
    close(fd);
    return kind;
}

/* ======================================================================== *
 *  Which syscalls are trapped and how their arguments look                  *
 * ======================================================================== */

#define NOP 0xff                /* no such argument */
#define CWD 0xfe                /* implicit AT_FDCWD */
enum { F_NO, F_YES, F_NOFLG, F_FOLLOWFLG, F_OPEN, F_OPEN2, F_INOTIFY };
enum { H_NONE, H_READLINK, H_GETCWD };

struct spec {
    long nr;
    unsigned char p1, d1, p2, d2;   /* path and dirfd argument indexes */
    unsigned char fol, fa;          /* how to derive "follow", and the flags argument */
    unsigned char hook;
};

#define S1(nr, p, d, fol, fa) { nr, p, d, NOP, CWD, fol, fa, H_NONE }
#define S2(nr, p, d, q, e, fol, fa) { nr, p, d, q, e, fol, fa, H_NONE }
#define SRL(nr, p, d) { nr, p, d, NOP, CWD, F_NO, 0, H_READLINK }

#ifndef SYS_openat2
#define SYS_openat2 437
#endif
#ifndef SYS_faccessat2
#define SYS_faccessat2 439
#endif
#ifndef SYS_fchmodat2
#define SYS_fchmodat2 452
#endif
#if !defined(SYS_statx) && defined(__x86_64__)
#define SYS_statx 332
#endif
#if !defined(SYS_statx) && defined(__aarch64__)
#define SYS_statx 291
#endif

static const struct spec specs[] = {
#ifdef SYS_open
    S1(SYS_open, 0, CWD, F_OPEN, 1),
#endif
    S1(SYS_openat, 1, 0, F_OPEN, 2),
    S1(SYS_openat2, 1, 0, F_OPEN2, 2),
#ifdef SYS_creat
    S1(SYS_creat, 0, CWD, F_YES, 0),
#endif
#ifdef SYS_stat
    S1(SYS_stat, 0, CWD, F_YES, 0),
#endif
#ifdef SYS_lstat
    S1(SYS_lstat, 0, CWD, F_NO, 0),
#endif
#ifdef SYS_newfstatat
    S1(SYS_newfstatat, 1, 0, F_NOFLG, 3),
#endif
    S1(SYS_statx, 1, 0, F_NOFLG, 2),
#ifdef SYS_access
    S1(SYS_access, 0, CWD, F_YES, 0),
#endif
    S1(SYS_faccessat, 1, 0, F_YES, 0),
    S1(SYS_faccessat2, 1, 0, F_NOFLG, 3),
#ifdef SYS_readlink
    SRL(SYS_readlink, 0, CWD),
#endif
    SRL(SYS_readlinkat, 1, 0),
    S1(SYS_chdir, 0, CWD, F_YES, 0),
#ifdef SYS_mkdir
    S1(SYS_mkdir, 0, CWD, F_NO, 0),
#endif
    S1(SYS_mkdirat, 1, 0, F_NO, 0),
#ifdef SYS_rmdir
    S1(SYS_rmdir, 0, CWD, F_NO, 0),
#endif
#ifdef SYS_unlink
    S1(SYS_unlink, 0, CWD, F_NO, 0),
#endif
    S1(SYS_unlinkat, 1, 0, F_NO, 0),
#ifdef SYS_rename
    S2(SYS_rename, 0, CWD, 1, CWD, F_NO, 0),
#endif
    S2(SYS_renameat, 1, 0, 3, 2, F_NO, 0),
    S2(SYS_renameat2, 1, 0, 3, 2, F_NO, 0),
#ifdef SYS_link
    S2(SYS_link, 0, CWD, 1, CWD, F_NO, 0),
#endif
    S2(SYS_linkat, 1, 0, 3, 2, F_FOLLOWFLG, 4),
#ifdef SYS_symlink
    S1(SYS_symlink, 1, CWD, F_NO, 0),
#endif
    S1(SYS_symlinkat, 2, 1, F_NO, 0),
#ifdef SYS_chmod
    S1(SYS_chmod, 0, CWD, F_YES, 0),
#endif
    S1(SYS_fchmodat, 1, 0, F_YES, 0),
    S1(SYS_fchmodat2, 1, 0, F_NOFLG, 3),
#ifdef SYS_chown
    S1(SYS_chown, 0, CWD, F_YES, 0),
#endif
#ifdef SYS_lchown
    S1(SYS_lchown, 0, CWD, F_NO, 0),
#endif
    S1(SYS_fchownat, 1, 0, F_NOFLG, 4),
    S1(SYS_truncate, 0, CWD, F_YES, 0),
    S1(SYS_utimensat, 1, 0, F_NOFLG, 3),
#ifdef SYS_utimes
    S1(SYS_utimes, 0, CWD, F_YES, 0),
#endif
#ifdef SYS_utime
    S1(SYS_utime, 0, CWD, F_YES, 0),
#endif
#ifdef SYS_futimesat
    S1(SYS_futimesat, 1, 0, F_YES, 0),
#endif
#ifdef SYS_mknod
    S1(SYS_mknod, 0, CWD, F_NO, 0),
#endif
    S1(SYS_mknodat, 1, 0, F_NO, 0),
    S1(SYS_statfs, 0, CWD, F_YES, 0),
    S1(SYS_setxattr, 0, CWD, F_YES, 0),
    S1(SYS_lsetxattr, 0, CWD, F_NO, 0),
    S1(SYS_getxattr, 0, CWD, F_YES, 0),
    S1(SYS_lgetxattr, 0, CWD, F_NO, 0),
    S1(SYS_listxattr, 0, CWD, F_YES, 0),
    S1(SYS_llistxattr, 0, CWD, F_NO, 0),
    S1(SYS_removexattr, 0, CWD, F_YES, 0),
    S1(SYS_lremovexattr, 0, CWD, F_NO, 0),
    S1(SYS_inotify_add_watch, 1, CWD, F_INOTIFY, 2),
    { SYS_getcwd, NOP, CWD, NOP, CWD, F_NO, 0, H_GETCWD },
};
#define NSPECS (sizeof specs / sizeof specs[0])

static const struct spec *find_spec(long nr)
{
    size_t i;
    for (i = 0; i < NSPECS; i++) if (specs[i].nr == nr) return &specs[i];
    return NULL;
}

/* The seccomp program: trap the syscalls above plus execve/execveat, allow the
   rest (and every foreign ABI, e.g. 32-bit compat or x32, which the tracer
   cannot interpret). */
static int install_filter(void)
{
    struct sock_filter f[NSPECS + 16];
    struct sock_fprog prog;
    long nrs[NSPECS + 2];
    size_t n = 0, i, k = 0;
    for (i = 0; i < NSPECS; i++) nrs[n++] = specs[i].nr;
    nrs[n++] = SYS_execve;
    nrs[n++] = SYS_execveat;
    f[k++] = (struct sock_filter) BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch));
    f[k++] = (struct sock_filter) BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SUP_AUDIT_ARCH, 1, 0);
    f[k++] = (struct sock_filter) BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    f[k++] = (struct sock_filter) BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr));
    for (i = 0; i < n; i++)
        f[k++] = (struct sock_filter) BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (unsigned) nrs[i], (unsigned char) (n - i), 0);
    f[k++] = (struct sock_filter) BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    f[k++] = (struct sock_filter) BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRACE);
    prog.len = (unsigned short) k;
    prog.filter = f;
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) return -errno;
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) < 0) return -errno;
    return 0;
}

/* ======================================================================== *
 *  Tasks and their scratch memory                                           *
 * ======================================================================== */

/* One address space the tracer has injected scratch memory into. Tasks that
   share an address space (threads, vfork children) share the struct. */
struct mm {
    unsigned long base;                 /* 0: none yet, NO_REGION: unavailable */
    int refs;
    pid_t arena_user;                   /* task currently using the exec arena */
    unsigned char used[NSLOTS / 8];
};

enum { S_RUN, S_FRESH, S_PREP, S_INJ, S_QUEUED };
enum { R_CONT, R_SYSCALL };

struct task {
    pid_t tid;
    struct task *hnext;
    int state;
    int xlate;                          /* paths are guest paths */
    int next_xlate;                     /* value committed by the next exec */
    struct mm *mm;
    int slot;
    int hook;                           /* result fix-up wanted at syscall exit */
    unsigned long ha, hb;               /* ... and its buffer / size */
    regs_t saved;                       /* regs of the syscall being re-executed */
    long saved_nr;
    struct task *qnext;
};

#define HASH 1024
static struct task *g_tab[HASH];
static struct task *g_inj;              /* task whose mmap is in flight */
static struct task *g_qhead, *g_qtail;  /* stopped tasks waiting for g_inj */

static struct task *task_find(pid_t tid)
{
    struct task *t;
    for (t = g_tab[(unsigned) tid % HASH]; t; t = t->hnext) if (t->tid == tid) return t;
    return NULL;
}

static struct mm *mm_new(void)
{
    struct mm *m = calloc(1, sizeof *m);
    if (!m) {
        wr("out of memory");
        exit(1);
    }
    m->refs = 1;
    return m;
}

static void mm_unref(struct mm *m)
{
    if (m && --m->refs == 0) free(m);
}

static struct task *task_new(pid_t tid)
{
    struct task *t = calloc(1, sizeof *t);
    if (!t) {
        wr("out of memory");
        exit(1);
    }
    t->tid = tid;
    t->slot = -1;
    t->mm = mm_new();
    t->hnext = g_tab[(unsigned) tid % HASH];
    g_tab[(unsigned) tid % HASH] = t;
    return t;
}

static void slot_free(struct task *t)
{
    if (t->slot >= 0 && t->mm) t->mm->used[t->slot / 8] &= (unsigned char) ~(1u << (t->slot % 8));
    t->slot = -1;
}

static int slot_alloc(struct task *t)
{
    int i;
    if (t->slot >= 0) return t->slot;
    for (i = 0; i < NSLOTS; i++) {
        if (!(t->mm->used[i / 8] & (1u << (i % 8)))) {
            t->mm->used[i / 8] |= (unsigned char) (1u << (i % 8));
            t->slot = i;
            return i;
        }
    }
    return -1;
}

static void queue_drop(struct task *t)
{
    struct task **pp = &g_qhead, *prev = NULL;
    while (*pp) {
        if (*pp == t) {
            *pp = t->qnext;
            if (g_qtail == t) g_qtail = prev;
            t->qnext = NULL;
            return;
        }
        prev = *pp;
        pp = &(*pp)->qnext;
    }
}

static void task_free(struct task *t)
{
    struct task **pp = &g_tab[(unsigned) t->tid % HASH];
    while (*pp && *pp != t) pp = &(*pp)->hnext;
    if (*pp) *pp = t->hnext;
    if (g_inj == t) g_inj = NULL;
    queue_drop(t);
    if (t->mm && t->mm->arena_user == t->tid) t->mm->arena_user = 0;
    slot_free(t);
    mm_unref(t->mm);
    free(t);
}

static int ntasks(void)
{
    int i, n = 0;
    for (i = 0; i < HASH; i++) {
        struct task *t;
        for (t = g_tab[i]; t; t = t->hnext) n++;
    }
    return n;
}

static void resume(struct task *t, int sig)
{
    int req = (t->state == S_INJ || t->hook) ? PTRACE_SYSCALL : PTRACE_CONT;
    if (ptrace(req, t->tid, 0, (void *) (long) sig) < 0 && errno != ESRCH)
        lg("resume %d: %s", (int) t->tid, strerror(errno));
}

/* Make the syscall a tracee is about to run return -err instead. */
static void skip(struct task *t, regs_t *r, int err)
{
    lg("%d: syscall %ld -> %s", (int) t->tid, reg_nr(r), strerror(-err));
    arch_set_nr(t->tid, r, -1);
    reg_set_ret(r, err);
    arch_setregs(t->tid, r);
    resume(t, 0);
}

/* ---- injecting an anonymous mapping into a tracee ---------------------- */

static void on_seccomp(struct task *t);

static void start_inject(struct task *t, regs_t *r)
{
    regs_t c = *r;
    t->saved = *r;
    t->saved_nr = reg_nr(r);
    reg_set_arg(&c, 0, 0);
    reg_set_arg(&c, 1, REGION_SZ);
    reg_set_arg(&c, 2, PROT_READ | PROT_WRITE);
    reg_set_arg(&c, 3, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE);
    reg_set_arg(&c, 4, (unsigned long) -1L);
    reg_set_arg(&c, 5, 0);
    arch_set_nr(t->tid, &c, SYS_mmap);
    arch_setregs(t->tid, &c);
    t->state = S_INJ;
    g_inj = t;
    resume(t, 0);
}

/* 1: scratch ready, 0: the task is parked until it is, -1: not available */
static int ensure_scratch(struct task *t, regs_t *r)
{
    if (t->mm->base == NO_REGION) return -1;
    if (t->mm->base) return slot_alloc(t) >= 0 ? 1 : -1;
    if (g_inj) {
        t->state = S_QUEUED;
        t->qnext = NULL;
        if (g_qtail) g_qtail->qnext = t;
        else g_qhead = t;
        g_qtail = t;
        return 0;
    }
    start_inject(t, r);
    return 0;
}

static void run_queue(void)
{
    while (g_qhead && !g_inj) {
        struct task *q = g_qhead;
        g_qhead = q->qnext;
        if (!g_qhead) g_qtail = NULL;
        q->qnext = NULL;
        q->state = S_RUN;
        on_seccomp(q);
    }
}

static unsigned long slot_addr(struct task *t) { return t->mm->base + (unsigned long) t->slot * SLOT_SZ; }
static unsigned long arena_addr(struct task *t) { return t->mm->base + (unsigned long) NSLOTS * SLOT_SZ; }

/* The injected mmap has finished: record the address and restart the syscall
   that was interrupted so that it traps again, now with scratch available. */
static void inject_done(struct task *t)
{
    regs_t r, s;
    long res;
    if (arch_getregs(t->tid, &r) < 0) return;
    if (reg_is_entry(&r)) {             /* tolerate an extra entry stop */
        resume(t, 0);
        return;
    }
    res = reg_ret(&r);
    if (res < 0 && res > -4096) {
        wr("cannot allocate scratch memory in pid %d: %s", (int) t->tid, strerror((int) -res));
        t->mm->base = NO_REGION;
    } else {
        t->mm->base = (unsigned long) res;
    }
    s = t->saved;
    reg_rewind(&s, t->saved_nr);
    arch_setregs(t->tid, &s);
    t->state = S_RUN;
    g_inj = NULL;
    resume(t, 0);
    run_queue();
}

/* ---- result fix-ups at syscall exit (getcwd, readlink) ----------------- */

static void hook_done(struct task *t)
{
    regs_t r;
    char buf[PM + 1], rb[PM];
    int kind = t->hook;
    long n;
    if (arch_getregs(t->tid, &r) < 0) return;
    if (reg_is_entry(&r)) {
        resume(t, 0);
        return;
    }
    t->hook = 0;
    n = reg_ret(&r);
    if (n > 0 && n <= PM) {
        size_t len = (size_t) n;
        if (kind == H_GETCWD && len > 0) len--;     /* the result counts the NUL */
        if (mem_read(t->tid, t->ha, buf, len) == 0) {
            const char *s;
            size_t sl;
            buf[len] = 0;
            s = x_back(buf, rb);
            sl = strlen(s);
            if (s != buf) {
                int fits = kind == H_GETCWD ? sl + 1 <= t->hb : sl <= t->hb;
                if (!fits) {
                    reg_set_ret(&r, kind == H_GETCWD ? -ERANGE : (long) t->hb);
                    if (kind == H_READLINK) sl = (size_t) t->hb;
                } else {
                    reg_set_ret(&r, (long) (kind == H_GETCWD ? sl + 1 : sl));
                }
                if (fits || kind == H_READLINK) {
                    if (mem_write(t->tid, t->ha, s, kind == H_GETCWD ? sl + 1 : sl) < 0) lg("hook write failed");
                }
                arch_setregs(t->tid, &r);
            }
        }
    }
    resume(t, 0);
}

/* ======================================================================== *
 *  Path syscalls                                                            *
 * ======================================================================== */

static int follow_for(struct task *t, const struct spec *sp, regs_t *r)
{
    unsigned long fl = sp->fa < 6 ? reg_arg(r, sp->fa) : 0;
    switch (sp->fol) {
    case F_NO: return 0;
    case F_YES: return 1;
    case F_NOFLG: return !(fl & AT_SYMLINK_NOFOLLOW);
    case F_FOLLOWFLG: return !!(fl & AT_SYMLINK_FOLLOW);
    case F_OPEN: return !((fl & O_NOFOLLOW) || ((fl & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)));
    case F_OPEN2: {
        unsigned long long how = 0;
        unsigned long p = reg_arg(r, sp->fa);
        if (!p || mem_read(t->tid, p, &how, sizeof how) < 0) return 1;
        return !((how & O_NOFOLLOW) || ((how & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)));
    }
    case F_INOTIFY: return !(fl & 0x02000000);          /* IN_DONT_FOLLOW */
    }
    return 1;
}

static int dfd_arg(const regs_t *r, unsigned char idx)
{
    return idx == CWD ? AT_FDCWD : (int) reg_arg(r, idx);
}

/* Fix the result of the syscall once it has run. */
static void arm_hook(struct task *t, const struct spec *sp, regs_t *r)
{
    if (sp->hook == H_READLINK) {
        t->hook = H_READLINK;
        t->ha = reg_arg(r, sp->p1 + 1);
        t->hb = reg_arg(r, sp->p1 + 2);
    } else if (sp->hook == H_GETCWD) {
        t->hook = H_GETCWD;
        t->ha = reg_arg(r, 0);
        t->hb = reg_arg(r, 1);
    }
}

static void do_path(struct task *t, regs_t *r, const struct spec *sp)
{
    char in1[PM], in2[PM], b1[PM], b2[PM];
    const char *h1 = NULL, *h2 = NULL;
    unsigned long a1 = 0, a2 = 0;
    int e, change = 0;
    ssize_t n;
    if (sp->p1 != NOP) {
        a1 = reg_arg(r, sp->p1);
        if (a1) {
            n = read_str(t->tid, a1, in1, sizeof in1);
            if (n == -EFAULT) a1 = 0;                       /* let the kernel report it */
            else if (n < 0) { skip(t, r, (int) n); return; }
            else {
                e = x_tr(t->tid, dfd_arg(r, sp->d1), in1, b1, follow_for(t, sp, r), &h1);
                if (e < 0) { skip(t, r, e); return; }
                if (h1 != in1) change |= 1;
            }
        }
    }
    if (sp->p2 != NOP) {
        a2 = reg_arg(r, sp->p2);
        if (a2) {
            n = read_str(t->tid, a2, in2, sizeof in2);
            if (n == -EFAULT) a2 = 0;
            else if (n < 0) { skip(t, r, (int) n); return; }
            else {
                e = x_tr(t->tid, dfd_arg(r, sp->d2), in2, b2, 0, &h2);
                if (e < 0) { skip(t, r, e); return; }
                if (h2 != in2) change |= 2;
            }
        }
    }
    if (change) {
        unsigned long s;
        e = ensure_scratch(t, r);
        if (e == 0) return;                                 /* parked: handled again later */
        if (e < 0) {
            wr("no scratch memory for pid %d, path not translated", (int) t->tid);
            skip(t, r, -ENOMEM);
            return;
        }
        s = slot_addr(t);
        if ((change & 1) && (mem_write(t->tid, s, h1, strlen(h1) + 1) < 0)) { resume(t, 0); return; }
        if ((change & 2) && (mem_write(t->tid, s + PM, h2, strlen(h2) + 1) < 0)) { resume(t, 0); return; }
        if (change & 1) reg_set_arg(r, sp->p1, s);
        if (change & 2) reg_set_arg(r, sp->p2, s + PM);
        lg("%d: syscall %ld %s%s%s -> %s%s%s", (int) t->tid, sp->nr, (change & 1) ? in1 : "", (change & 2) ? " " : "",
           (change & 2) ? in2 : "", (change & 1) ? h1 : "", (change & 2) ? " " : "", (change & 2) ? h2 : "");
        arch_setregs(t->tid, r);
    }
    arm_hook(t, sp, r);
    resume(t, 0);
}

/* ======================================================================== *
 *  execve                                                                   *
 * ======================================================================== */

/* Does any of the n env entries start with `name` (which ends in '=')? */
static int env_has(pid_t pid, const unsigned long *ev, long n, const char *name)
{
    size_t l = strlen(name);
    long i;
    char b[64];
    for (i = 0; i < n; i++) {
        int fd = mem_fd(pid);
        ssize_t k;
        if (fd < 0) return 0;
        k = pread(fd, b, l < sizeof b ? l : sizeof b, (off_t) ev[i]);
        if (k == (ssize_t) l && !memcmp(b, name, l)) return 1;
    }
    return 0;
}

struct blob {
    char *buf;
    size_t len, cap;
    unsigned long base;                 /* remote address of buf[0] */
};

static int blob_room(struct blob *b, size_t n)
{
    return b->len + n + 16 <= b->cap;
}

static unsigned long blob_str(struct blob *b, const char *s)
{
    size_t n = strlen(s) + 1;
    unsigned long a;
    if (!blob_room(b, n)) return 0;
    memcpy(b->buf + b->len, s, n);
    a = b->base + b->len;
    b->len += n;
    return a;
}

static unsigned long blob_ptrs(struct blob *b, const unsigned long *p, size_t n)
{
    size_t bytes = (n + 1) * sizeof(unsigned long);
    unsigned long a;
    b->len = (b->len + 7) & ~(size_t) 7;
    if (!blob_room(b, bytes)) return 0;
    memcpy(b->buf + b->len, p, n * sizeof(unsigned long));
    memset(b->buf + b->len + n * sizeof(unsigned long), 0, sizeof(unsigned long));
    a = b->base + b->len;
    b->len += bytes;
    return a;
}

/* Variables a guest shim needs to find its configuration again. If the
   program being exec'ed dropped them from its environment they are restored
   from the supervisor's own. */
static const char *const keep_vars[] = {
    "PRX_ROOTFS", "PRX_SHIM", "PRX_LIBPATH", "PRX_BIND", "PRX_FAKEROOT", "PRX_DNS", "PRX_SELF_EXE",
    "PRX_LOADER", "PRX_NO_ARGV0", "PRX_VUSER", "PRX_CRED", "PRX_SUP", "PRX_LD", "PRX_SH", "PRX_SESSION",
};
#define NKEEP (sizeof keep_vars / sizeof keep_vars[0])

/* The program is not a static ELF: run the guest shell under the loader with
   the shim preloaded and PRX_DELEGATE=1. The shim's constructor performs the
   real execve(path, argv, envp) from inside the guest. Returns 0 when the
   syscall registers have been rewritten (or the task parked), else -errno. */
static int delegate_exec(struct task *t, regs_t *r, int isat, const char *gpath,
                         unsigned long avp, unsigned long evp)
{
    static unsigned long oav[MAXPTRS], oev[MAXPTRS], nav[MAXPTRS + 8], nev[MAXPTRS + 32];
    struct blob b;
    char *lp_env = NULL;
    unsigned long a_ld, a_lpk, a_lp, a_sh, a_path, a_av, a_ev, arena;
    long argc, envc, i, ne = 0, na = 0;
    size_t j;
    int found[NKEEP] = { 0 }, have_preload = 0, e;
    if (!g_ld[0] || !g_sh[0] || !g_shim[0]) return -ENOEXEC;
    argc = avp ? read_ptrs(t->tid, avp, oav, MAXPTRS) : 0;
    envc = evp ? read_ptrs(t->tid, evp, oev, MAXPTRS) : 0;
    if (argc < 0 || envc < 0) return -E2BIG;
    e = ensure_scratch(t, r);
    if (e == 0) return 1;
    if (e < 0) return -ENOMEM;
    if (t->mm->arena_user && t->mm->arena_user != t->tid) return -EAGAIN;
    t->mm->arena_user = t->tid;
    arena = arena_addr(t);
    b.buf = malloc(ARENA_SZ);
    if (!b.buf) return -ENOMEM;
    b.len = 0;
    b.cap = ARENA_SZ;
    b.base = arena;
    a_ld = blob_str(&b, g_ld);
    a_lpk = blob_str(&b, "--library-path");
    a_lp = blob_str(&b, g_libpath);
    a_sh = blob_str(&b, g_sh);
    a_path = blob_str(&b, gpath);
    if (!a_ld || !a_lpk || !a_lp || !a_sh || !a_path) goto toobig;
    nav[na++] = a_ld;
    nav[na++] = a_lpk;
    nav[na++] = a_lp;
    nav[na++] = a_sh;
    nav[na++] = a_path;
    for (i = 0; i < argc; i++) nav[na++] = oav[i];
    /* environment: keep the program's own, make sure the shim is preloaded */
    for (i = 0; i < envc; i++) {
        char hd[64];
        int fd = mem_fd(t->tid);
        ssize_t k = fd < 0 ? -1 : pread(fd, hd, sizeof hd - 1, (off_t) oev[i]);
        if (k > 0) {
            hd[k] = 0;
            if (!strncmp(hd, "LD_PRELOAD=", 11)) {
                char *full = malloc(PM * 4);
                ssize_t m = full ? read_str(t->tid, oev[i], full, PM * 4) : -ENOMEM;
                if (m >= 0) {
                    lp_env = full;
                    have_preload = 1;
                    continue;
                }
                free(full);
            }
            if (!strncmp(hd, "PRX_DELEGATE=", 13) || !strncmp(hd, "PRX_TRACED=", 11)) continue;
            for (j = 0; j < NKEEP; j++) {
                size_t l = strlen(keep_vars[j]);
                if (!strncmp(hd, keep_vars[j], l) && hd[l] == '=') found[j] = 1;
            }
        }
        nev[ne++] = oev[i];
    }
    {
        char *pre;
        const char *old = have_preload ? lp_env + 11 : "";
        size_t need = strlen(g_shim) + strlen(old) + 16;
        unsigned long a;
        int has_shim = 0;
        const char *q = old;
        pre = malloc(need);
        if (!pre) { e = -ENOMEM; goto fail; }
        while (*q) {
            const char *z = q;
            while (*z && *z != ':') z++;
            if ((size_t) (z - q) == strlen(g_shim) && !memcmp(q, g_shim, (size_t) (z - q))) has_shim = 1;
            q = *z ? z + 1 : z;
        }
        if (*old && has_shim) snprintf(pre, need, "LD_PRELOAD=%s", old);
        else if (*old) snprintf(pre, need, "LD_PRELOAD=%s:%s", g_shim, old);
        else snprintf(pre, need, "LD_PRELOAD=%s", g_shim);
        a = blob_str(&b, pre);
        free(pre);
        if (!a) goto toobig;
        nev[ne++] = a;
    }
    for (j = 0; j < NKEEP; j++) {
        const char *v;
        char *kv;
        unsigned long a;
        if (found[j]) continue;
        v = getenv(keep_vars[j]);
        if (!v) continue;
        kv = malloc(strlen(keep_vars[j]) + strlen(v) + 2);
        if (!kv) { e = -ENOMEM; goto fail; }
        sprintf(kv, "%s=%s", keep_vars[j], v);
        a = blob_str(&b, kv);
        free(kv);
        if (!a) goto toobig;
        nev[ne++] = a;
    }
    {
        unsigned long a1 = blob_str(&b, "PRX_DELEGATE=1"), a2 = blob_str(&b, "PRX_TRACED=1");
        if (!a1 || !a2) goto toobig;
        nev[ne++] = a1;
        nev[ne++] = a2;
    }
    a_av = blob_ptrs(&b, nav, (size_t) na);
    a_ev = blob_ptrs(&b, nev, (size_t) ne);
    if (!a_av || !a_ev) goto toobig;
    if (mem_write(t->tid, arena, b.buf, b.len) < 0) { e = -EFAULT; goto fail; }
    free(b.buf);
    free(lp_env);
    reg_set_arg(r, isat ? 1 : 0, a_ld);
    reg_set_arg(r, isat ? 2 : 1, a_av);
    reg_set_arg(r, isat ? 3 : 2, a_ev);
    if (isat) {
        reg_set_arg(r, 0, (unsigned long) AT_FDCWD);
        reg_set_arg(r, 4, 0);
    }
    arch_setregs(t->tid, r);
    return 0;
toobig:
    e = -E2BIG;
fail:
    free(b.buf);
    free(lp_env);
    t->mm->arena_user = 0;
    return e;
}

static void do_exec(struct task *t, regs_t *r, long nr)
{
    int isat = nr == SYS_execveat, dfd = isat ? (int) reg_arg(r, 0) : AT_FDCWD;
    unsigned long pp = reg_arg(r, isat ? 1 : 0), avp = reg_arg(r, isat ? 2 : 1), evp = reg_arg(r, isat ? 3 : 2);
    int flags = isat ? (int) reg_arg(r, 4) : 0;
    char path[PM], hb[PM];
    const char *h;
    ssize_t n = pp ? read_str(t->tid, pp, path, sizeof path) : -EFAULT;
    int e;
    t->next_xlate = 0;
    if (!t->xlate) {
        /* shim-managed or native caller: the path is a host path already. The
           new image is "xlate" only if it is a static guest program, i.e. a
           static ELF started with the shim's environment. */
        if (n >= 0 && evp) {
            static unsigned long ev[MAXPTRS];
            long c = read_ptrs(t->tid, evp, ev, MAXPTRS);
            if (c >= 0 && env_has(t->tid, ev, c, "PRX_ROOTFS=") && exec_kind(path) == K_STATIC) t->next_xlate = 1;
        }
        resume(t, 0);
        return;
    }
    if (n < 0 && !(isat && (flags & AT_EMPTY_PATH))) { resume(t, 0); return; }
    if (isat && (flags & AT_EMPTY_PATH) && (n <= 0)) {     /* fexecve: nothing to translate */
        t->next_xlate = 1;
        resume(t, 0);
        return;
    }
    e = x_tr(t->tid, dfd, path, hb, 1, &h);
    if (e < 0) { skip(t, r, e); return; }
    if (access(h, X_OK) < 0) { skip(t, r, -errno); return; }
    if (exec_kind(h) == K_STATIC) {
        t->next_xlate = 1;
        if (h != path) {
            e = ensure_scratch(t, r);
            if (e == 0) return;
            if (e < 0) { skip(t, r, -ENOMEM); return; }
            if (mem_write(t->tid, slot_addr(t), h, strlen(h) + 1) < 0) { resume(t, 0); return; }
            reg_set_arg(r, isat ? 1 : 0, slot_addr(t));
            if (isat) {
                reg_set_arg(r, 0, (unsigned long) AT_FDCWD);
                reg_set_arg(r, 4, (unsigned long) (flags & ~AT_EMPTY_PATH));
            }
            arch_setregs(t->tid, r);
            lg("%d: exec %s -> %s (static)", (int) t->tid, path, h);
        }
        resume(t, 0);
        return;
    }
    {
        /* the shim wants the absolute guest path, as execve() would have seen it */
        char gabs[PM];
        const char *gp = path;
        if (path[0] != '/' && x_mkabs(t->tid, dfd, path, gabs) == 0) gp = gabs;
        e = delegate_exec(t, r, isat, gp, avp, evp);
        if (e == 1) return;                                 /* parked */
        if (e < 0) { skip(t, r, e); return; }
        lg("%d: exec %s -> delegated to the shim", (int) t->tid, gp);
        resume(t, 0);
    }
}

/* ======================================================================== *
 *  The event loop                                                           *
 * ======================================================================== */

static void on_seccomp(struct task *t)
{
    regs_t r;
    long nr;
    const struct spec *sp;
    if (arch_getregs(t->tid, &r) < 0) {
        resume(t, 0);
        return;
    }
    nr = reg_nr(&r);
    if (g_probe) {
        skip(t, &r, -EIO);
        return;
    }
    if (nr == SYS_execve || nr == SYS_execveat) {
        do_exec(t, &r, nr);
        return;
    }
    if (!t->xlate || !(sp = find_spec(nr))) {
        resume(t, 0);
        return;
    }
    do_path(t, &r, sp);
}

static void inherit(struct task *c, struct task *p, int share_mm)
{
    c->xlate = p->xlate;
    c->next_xlate = 0;
    mm_unref(c->mm);
    if (share_mm) {
        c->mm = p->mm;
        c->mm->refs++;
        c->slot = -1;
    } else {
        c->mm = mm_new();
        c->mm->base = p->mm->base;
        memcpy(c->mm->used, p->mm->used, sizeof c->mm->used);
        c->slot = p->slot;
    }
}

static int clone_shares_mm(struct task *t, int ev)
{
    regs_t r;
    long nr;
    unsigned long fl;
    if (ev == SUP_EV_VFORK) return 1;
    if (ev == SUP_EV_FORK) return 0;
    if (arch_getregs(t->tid, &r) < 0) return 1;
    nr = reg_nr(&r);
#ifdef SYS_clone3
    if (nr == SYS_clone3) {
        unsigned long long f = 0;
        if (mem_read(t->tid, reg_arg(&r, 0), &f, sizeof f) < 0) return 1;
        return !!(f & CLONE_VM);
    }
#endif
    fl = reg_arg(&r, 0);
    return !!(fl & CLONE_VM);
}

static void on_fork(struct task *t, int ev)
{
    unsigned long msg = 0;
    struct task *c;
    int share;
    if (ptrace(PTRACE_GETEVENTMSG, t->tid, 0, &msg) < 0) {
        resume(t, 0);
        return;
    }
    share = clone_shares_mm(t, ev);
    c = task_find((pid_t) msg);
    if (!c) {
        c = task_new((pid_t) msg);
        c->state = S_PREP;
        inherit(c, t, share);
    } else {                                                /* its first stop came first */
        inherit(c, t, share);
        c->state = S_RUN;
        ptrace(PTRACE_CONT, c->tid, 0, 0);
    }
    resume(t, 0);
}

static void on_exec(struct task *t)
{
    unsigned long msg = 0;
    struct task *old;
    ptrace(PTRACE_GETEVENTMSG, t->tid, 0, &msg);
    old = task_find((pid_t) msg);
    if (old && old != t) {
        /* a non-leader thread exec'ed: it took over the leader's pid, so its
           record (which holds the state of that execve) moves to t */
        t->next_xlate = old->next_xlate;
        task_free(old);
    }
    slot_free(t);
    if (t->mm->arena_user == t->tid) t->mm->arena_user = 0;
    mm_unref(t->mm);
    t->mm = mm_new();
    t->hook = 0;
    t->xlate = t->next_xlate;
    t->next_xlate = 0;
    lg("%d: exec done, %s", (int) t->tid, t->xlate ? "static (translated)" : "managed");
    resume(t, 0);
}

static void on_stop(struct task *t, int sig, int ev)
{
    if (t->mm && t->mm->arena_user == t->tid && ev != SUP_EV_SECCOMP) t->mm->arena_user = 0;
    if (ev == SUP_EV_SECCOMP) {
        if (t->mm->arena_user == t->tid) t->mm->arena_user = 0;
        on_seccomp(t);
    } else if (ev == SUP_EV_EXEC) {
        on_exec(t);
    } else if (ev == SUP_EV_FORK || ev == SUP_EV_VFORK || ev == SUP_EV_CLONE) {
        on_fork(t, ev);
    } else if (ev == SUP_EV_STOP) {
        if (sig == SIGTRAP) {                               /* start of a new tracee, or SIGCONT after a group-stop */
            if (t->state == S_PREP) t->state = S_RUN;
            resume(t, 0);
        } else {                                            /* group-stop: stay stopped, but hear SIGCONT */
            if (ptrace((enum __ptrace_request) SUP_LISTEN, t->tid, 0, 0) < 0) resume(t, 0);
        }
    } else if (sig == (SIGTRAP | 0x80)) {
        if (t->state == S_INJ) inject_done(t);
        else if (t->hook) hook_done(t);
        else resume(t, 0);
    } else {
        resume(t, sig);                                     /* signal-delivery-stop: pass it on */
    }
}

static void event_loop(void)
{
    for (;;) {
        int st;
        pid_t pid = waitpid(-1, &st, __WALL);
        struct task *t;
        if (pid < 0) {
            if (errno == EINTR) continue;
            break;
        }
        t = task_find(pid);
        if (WIFEXITED(st) || WIFSIGNALED(st)) {
            if (t) task_free(t);
            mem_done();
            if (!ntasks()) break;
            continue;
        }
        if (!WIFSTOPPED(st)) continue;
        if (!t) {
            /* an auto-attached child that stopped before its parent's fork event
               was seen: keep it stopped until the parent's event tells us what it is */
            t = task_new(pid);
            t->state = S_FRESH;
            if ((st >> 16) == SUP_EV_STOP) continue;
        }
            on_stop(t, WSTOPSIG(st), st >> 16);
        mem_done();
    }
}

/* ======================================================================== *
 *  Launcher: `prxsup run`                                                   *
 * ======================================================================== */

static const char *g_why = "";

/* Fork the tracer and let it seize us. Returns 0 on success, else -errno with
   g_why naming the step that failed. The tracer is a grandchild: it is no one's
   child and runs in its own session, so neither wait() calls in the guest nor
   terminal signals can disturb it. */
static int attach_self(void)
{
    int a[2], b[2], err;
    pid_t x = getpid(), s1, s2;
    char c;
    if (pipe2(a, O_CLOEXEC) < 0 || pipe2(b, O_CLOEXEC) < 0) {
        g_why = "pipe";
        return -errno;
    }
    s1 = fork();
    if (s1 < 0) {
        g_why = "fork";
        return -errno;
    }
    if (s1 == 0) {
        s2 = fork();
        if (s2 != 0) _exit(s2 < 0 ? 1 : 0);
        /* ---- tracer ---- */
        {
            int e = 0;
            pid_t me = getpid();
            setsid();
            signal(SIGPIPE, SIG_IGN);
            if (write(b[1], &me, sizeof me) != sizeof me) _exit(1);
            if (read(a[0], &c, 1) != 1) _exit(1);           /* "go": PR_SET_PTRACER is set */
            if (ptrace((enum __ptrace_request) SUP_SEIZE, x, 0,
                       (void *) (long) (SUP_O_TRACESYSGOOD | SUP_O_TRACEFORK | SUP_O_TRACEVFORK | SUP_O_TRACECLONE
                                        | SUP_O_TRACEEXEC | SUP_O_TRACESECCOMP | SUP_O_EXITKILL)) < 0) {
                e = errno;
                if (write(b[1], &e, sizeof e) < 0) { /* nothing to do */ }
                _exit(1);
            }
            if (write(b[1], &e, sizeof e) != sizeof e) _exit(1);
            close(a[0]);
            close(b[1]);
            task_new(x);
            event_loop();
            _exit(0);
        }
    }
    close(a[0]);
    close(b[1]);
    {
        int st;
        pid_t me = 0;
        waitpid(s1, &st, 0);
        if (read(b[0], &me, sizeof me) != sizeof me) {
            g_why = "tracer start";
            close(a[1]);
            close(b[0]);
            return -ECHILD;
        }
        /* Yama (ptrace_scope 1) only lets ancestors trace; the grandchild is none. */
        prctl(PR_SET_PTRACER, (unsigned long) me, 0, 0, 0);
        c = 'g';
        if (write(a[1], &c, 1) != 1) {
            close(a[1]);
            close(b[0]);
            g_why = "tracer start";
            return -EPIPE;
        }
        if (read(b[0], &err, sizeof err) != sizeof err) {
            g_why = "tracer attach";
            close(a[1]);
            close(b[0]);
            return -ECHILD;
        }
        close(a[1]);
        close(b[0]);
        if (err) {
            g_why = "PTRACE_SEIZE";
            return -err;
        }
    }
    return 0;
}

static int cmd_run(int argc, char **argv)
{
    const char *target;
    const char *tr;
    if (argc > 0 && !strcmp(argv[0], "--")) {
        argc--;
        argv++;
    }
    if (argc < 2) {
        wr("usage: prxsup run [--] PATH ARGV0 [ARGS...]");
        return 2;
    }
    restore_env();
    target = argv[0];
    tr = getenv("PRX_TRACED");
    if (tr && *tr) return plain_exec(target, argv + 1);     /* already traced (or tracing failed once) */
    if (!cfg_load() || exec_kind(target) != K_STATIC) return plain_exec(target, argv + 1);
    {
        int e = attach_self();
        if (e < 0) {
            wr("warning: cannot trace static programs (%s: %s); they run without path translation", g_why,
               strerror(-e));
            setenv("PRX_TRACED", "off", 1);
        } else if ((e = install_filter()) < 0) {
            wr("warning: cannot install the seccomp filter (%s); static programs run without path translation",
               strerror(-e));
            setenv("PRX_TRACED", "off", 1);
        } else {
            setenv("PRX_TRACED", "1", 1);
        }
    }
    return plain_exec(target, argv + 1);
}

/* `prxsup probe`: does ptrace + seccomp work here? One seccomp stop is
   provoked and answered with EIO by the tracer. */
static int cmd_probe(void)
{
    pid_t c;
    int st, e;
    g_probe = 1;
    c = fork();
    if (c < 0) {
        wr("probe: fork: %s", strerror(errno));
        return 1;
    }
    if (c == 0) {
        long r;
        e = attach_self();
        if (e < 0) {
            wr("probe: ptrace attach failed (%s: %s)", g_why, strerror(-e));
            if (!strcmp(g_why, "PTRACE_SEIZE") && -e == EPERM)
                wr("probe: see /proc/sys/kernel/yama/ptrace_scope, or a security policy that forbids ptrace");
            _exit(10);
        }
        e = install_filter();
        if (e < 0) {
            wr("probe: cannot install a seccomp filter: %s", strerror(-e));
            _exit(11);
        }
        errno = 0;
        r = syscall(SYS_readlinkat, AT_FDCWD, "/", NULL, 0);
        if (r != -1 || errno != EIO) {
            wr("probe: the tracer did not see the seccomp stop (result %ld, errno %d)", r, errno);
            _exit(12);
        }
        _exit(0);
    }
    if (waitpid(c, &st, 0) < 0) return 1;
    return WIFEXITED(st) && WEXITSTATUS(st) == 0 ? 0 : 1;
}

#endif /* SUP_ARCH_OK */

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "--version")) {
        puts("prxsup " SUP_VERSION);
        return 0;
    }
#ifdef SUP_ARCH_OK
    if (argc >= 2 && !strcmp(argv[1], "run")) return cmd_run(argc - 2, argv + 2);
    if (argc >= 2 && !strcmp(argv[1], "probe")) return cmd_probe();
    fputs("usage: prxsup run [--] PATH ARGV0 [ARGS...] | probe | --version\n"
          "(internal helper of prx, started with `prx login --ptrace`)\n", stderr);
    return 2;
#else
    if (argc >= 5 && !strcmp(argv[1], "run")) {         /* never get in the way of the program */
        int i = 2;
        if (!strcmp(argv[i], "--")) i++;
        restore_env();
        return plain_exec(argv[i], argv + i + 1);
    }
    wr("this architecture is not supported");
    return 2;
#endif
}
