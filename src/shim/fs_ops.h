/* fs_ops.h - access, readlink, cwd, realpath, rename, link, chmod, chown, mknod, mount, xattr, statfs
 * Part of the prx shim (unity build: included by src/prx.c, not a standalone header). */
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
