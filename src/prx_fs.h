/* prx_fs.h - interposed filesystem entry points
 *
 * open/stat/access/readlink/getcwd/realpath, mkdir/unlink/rename/link,
 * chmod/chown/truncate/utimensat/mknod, mount and statfs, xattr, ioctl and
 * tty calls, fcntl, glob/scandir/mkstemp, plus the AF_UNIX address helpers
 * (un_fix and friends) that the socket calls in prx_net.h share.
 *
 * Part of the shim. Included only by prx.c, in the order listed there:
 * the shim is one translation unit and later parts use what earlier
 * parts define. Do not compile this file on its own.
 */
#ifndef PRX_FS_H
#define PRX_FS_H

static int do_open(int dfd, const char*p, int fl, int mode) {
    if (p) {
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

#endif /* PRX_FS_H */
