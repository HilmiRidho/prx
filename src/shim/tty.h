/* tty.h - isatty, tcgetattr, tcsetattr
 * Part of the prx shim (unity build: included by src/prx.c, not a standalone header). */
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
