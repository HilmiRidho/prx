/* fs_open.h - open/fopen/dlopen family
 * Part of the prx shim (unity build: included by src/prx.c, not a standalone header). */
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
