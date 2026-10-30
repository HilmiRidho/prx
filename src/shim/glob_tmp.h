/* glob_tmp.h - glob, scandir, mkstemp, mkdtemp
 * Part of the prx shim (unity build: included by src/prx.c, not a standalone header). */
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
