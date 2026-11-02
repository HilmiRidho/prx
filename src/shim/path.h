/* path.h - guest->host path translation (tr, canon, binds)
 * Part of the prx shim (unity build: included by src/prx.c, not a standalone header). */
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
