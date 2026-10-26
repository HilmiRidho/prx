/* prx_exec.h - exec, spawn and library-path handling
 *
 * Environment filtering, ELF inspection, the library path, prep_exec() and
 * the execve, execvp, execl*, posix_spawn, system and popen families.
 *
 * Part of the shim. Included only by prx.c, in the order listed there:
 * the shim is one translation unit and later parts use what earlier
 * parts define. Do not compile this file on its own.
 */
#ifndef PRX_EXEC_H
#define PRX_EXEC_H

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
    return envmatch(e, "LD_PRELOAD") || envmatch(e, "PRX_ROOTFS") || envmatch(e, "PRX_SHIM") || envmatch(e, "PRX_LIBPATH") || envmatch(e, "PRX_BIND") || envmatch(e, "PRX_FAKEROOT") || envmatch(e, "PRX_DNS") || envmatch(e, "PRX_LOADER") || envmatch(e, "PRX_QEMU") || envmatch(e, "QEMU_SET_ENV") || envmatch(e, "PRX_SELF_EXE") || envmatch(e, "PRX_VUSER") || envmatch(e, "PRX_CRED") || envmatch(e, "PRX_NO_ARGV0") || envmatch(e, "PRX_SUP") || envmatch(e, "PRX_LD") || envmatch(e, "PRX_SH") || envmatch(e, "PRX_TRACED") || envmatch(e, "PRX_DELEGATE");
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
        if (g_sup[0]) ne[k++] = acat(a, "PRX_SUP=", g_sup);
        if (g_ld[0]) ne[k++] = acat(a, "PRX_LD=", g_ld);
        if (g_sh[0]) ne[k++] = acat(a, "PRX_SH=", g_sh);
        if (g_traced[0]) ne[k++] = acat(a, "PRX_TRACED=", g_traced);
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
            } else if (g_sup[0] && !g_traced[0]) {
                /* --ptrace: let the supervisor decide whether this really is a
                   static program and, if so, trace it (it keeps our pid) */
                char**sv = am(a, (size_t)(argc+8)*sizeof (char*));
                if (!sv) return (int) fail(ENOMEM);
                int sk = 0;
                sv[sk++] = g_sup;
                sv[sk++] = "run";
                sv[sk++] = "--";
                sv[sk++] = (char*) h;
                for (int j = 0; j<argc; j++) sv[sk++] = av[j];
                sv[sk] = NULL;
                *op = g_sup;
                *oa = sv;
                exec_cred_prepare(h);
                *oe = mkenv(a, envp, 0, gp);
                if (!*oe) return (int) fail(ENOMEM);
                /* the supervisor is a host program: it must not load the shim or
                   guest libraries, so it gets these two under another name */
                for (char**ev = *oe; *ev; ev++) {
                    if (envmatch(*ev, "LD_PRELOAD") || envmatch(*ev, "LD_LIBRARY_PATH")) {
                        char*r = acat(a, "PRX_SUP_", *ev);
                        if (!r) return (int) fail(ENOMEM);
                        *ev = r;
                    }
                }
                return 0;
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
/* Constructor side of the supervisor's exec delegation (prxsup.c): this process
   is the guest shell started under the loader with PRX_DELEGATE=1 and argv =
   { shell, PATH, ORIGINAL-ARGV... }. Do the execve() the traced program asked
   for, with the full shim logic (shebangs, loader, credentials, ...). */
static void prx_delegate(int argc, char**argv) {
    char*fb[2];
    char*const*av = argv+2;
    if (argc < 2) return;
    if (argc == 2) {
        fb[0] = argv[1];
        fb[1] = NULL;
        av = fb;
    }
    do_execve(argv[1], av, environ);
    int e = geterr();
    static const char m1[] = "prx: cannot execute ";
    sc(N_write, 2, (long) m1, (long) sizeof m1-1, 0, 0, 0);
    sc(N_write, 2, (long) argv[1], (long) strlen(argv[1]), 0, 0, 0);
    sc(N_write, 2, (long) "\n", 1, 0, 0, 0);
    sc(N_exit_group, (e == ENOENT || e == ENOTDIR) ? 127 : 126, 0, 0, 0, 0, 0);
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

#endif /* PRX_EXEC_H */
