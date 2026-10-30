/* fs_stat.h - stat/statx family and ownership fix-ups
 * Part of the prx shim (unity build: included by src/prx.c, not a standalone header). */
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
