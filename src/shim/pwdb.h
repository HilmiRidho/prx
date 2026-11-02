/* pwdb.h - passwd/group lookups and enumeration, credential encoding
 * Part of the prx shim (unity build: included by src/prx.c, not a standalone header). */
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
    const char*h;
    if (lenp)*lenp = 0;
    if (!g_rl) return NULL;
    /* Resolve symlinks as the guest would (absolute links stay inside the
       rootfs); a plain root+rel open would follow e.g. /etc/passwd -> /host/x
       out of the rootfs. */
    int sv = geterr();
    if (tr(AT_FDCWD, rel, p, 1, &h)<0) {
        *__errno_location() = sv;
        return NULL;
    }
    long fd = SC4(N_openat, AT_FDCWD, h, O_RDONLY|O_CLOEXEC, 0);
    if (fd<0) return NULL;
    size_t cap = 16384, len = 0;
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
/* byuid: 0 = by name, 1 = by uid, 2 = the uid-th valid entry (enumeration).
   Returns 1 found, 0 not found, -1 buffer too small, -2 database unreadable. */
static int pw_lookup(const char*name, int byuid, u32 uid, struct pw*o, char*buf, size_t bl) {
    u32 idx = 0;
    long n;
    char*data = read_rel_all("/etc/passwd", &n);
    if (!data) return-2;
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
        if (k == 7 && (byuid == 2 ? idx++ == uid : ((byuid && atou(f[2]) == uid) || (!byuid && !strcmp(f[0], name))))) {
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
    u32 idx = 0;
    long n;
    char*data = read_rel_all("/etc/group", &n);
    if (!data) return-2;
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
        if (k == 4 && (bygid == 2 ? idx++ == gid : ((bygid && atou(f[2]) == gid) || (!bygid && !strcmp(f[0], name))))) {
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
        if (r != -1 || s_pwcap >= (1UL<<26)) return r;
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
        if (r != -1 || s_grcap >= (1UL<<26)) return r;
        char*nb = realloc(s_grbuf, s_grcap*2);
        if (!nb) return-1;
        s_grbuf = nb;
        s_grcap *= 2;
    }
}
EXP struct pw*getpwuid(u32 uid) {
    static struct pw*(*f)(u32);
    if (g_rl) {
        int r = pw_lookup_dyn(NULL, 1, uid);
        if (r == 1) return&s_pw;
        if (r == 0) return NULL; /* readable database, no such entry: never ask the host */
    }
    if (!f) f = nextsym("getpwuid");
    return f(uid);
}
EXP struct pw*getpwnam(const char*n) {
    static struct pw*(*f)(const char*);
    if (g_rl) {
        int r = pw_lookup_dyn(n, 0, 0);
        if (r == 1) return&s_pw;
        if (r == 0) return NULL; /* readable database, no such entry: never ask the host */
    }
    if (!f) f = nextsym("getpwnam");
    return f(n);
}
EXP struct gr*getgrgid(u32 gid) {
    static struct gr*(*f)(u32);
    if (g_rl) {
        int r = gr_lookup_dyn(NULL, 1, gid);
        if (r == 1) return&s_gr;
        if (r == 0) return NULL; /* readable database, no such entry: never ask the host */
    }
    if (!f) f = nextsym("getgrgid");
    return f(gid);
}
EXP struct gr*getgrnam(const char*n) {
    static struct gr*(*f)(const char*);
    if (g_rl) {
        int r = gr_lookup_dyn(n, 0, 0);
        if (r == 1) return&s_gr;
        if (r == 0) return NULL; /* readable database, no such entry: never ask the host */
    }
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
        if (r == 0) {
            *res = NULL;
            return 0;
        }
        if (r == -1) {
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
        if (r == 0) {
            *res = NULL;
            return 0;
        }
        if (r == -1) {
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
        if (r == 0) {
            *res = NULL;
            return 0;
        }
        if (r == -1) {
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
        if (r == 0) {
            *res = NULL;
            return 0;
        }
        if (r == -1) {
            *res = NULL;
            return ERANGE;
        }
    }
    if (!f) f = nextsym("getgrgid_r");
    return f(gid, gr, buf, bl, res);
}
static u32 s_pwi, s_gri;
EXP void setpwent(void) {
    static void(*f)(void);
    if (g_rl) {
        s_pwi = 0;
        return;
    }
    if (!f) f = nextsym("setpwent");
    if (f) f();
}
EXP void endpwent(void) {
    static void(*f)(void);
    if (g_rl) {
        s_pwi = 0;
        return;
    }
    if (!f) f = nextsym("endpwent");
    if (f) f();
}
EXP struct pw*getpwent(void) {
    static struct pw*(*f)(void);
    if (g_rl) {
        if (pw_lookup_dyn(NULL, 2, s_pwi) != 1) return NULL;
        s_pwi++;
        return&s_pw;
    }
    if (!f) f = nextsym("getpwent");
    return f ? f() : NULL;
}
EXP void setgrent(void) {
    static void(*f)(void);
    if (g_rl) {
        s_gri = 0;
        return;
    }
    if (!f) f = nextsym("setgrent");
    if (f) f();
}
EXP void endgrent(void) {
    static void(*f)(void);
    if (g_rl) {
        s_gri = 0;
        return;
    }
    if (!f) f = nextsym("endgrent");
    if (f) f();
}
EXP struct gr*getgrent(void) {
    static struct gr*(*f)(void);
    if (g_rl) {
        if (gr_lookup_dyn(NULL, 2, s_gri) != 1) return NULL;
        s_gri++;
        return&s_gr;
    }
    if (!f) f = nextsym("getgrent");
    return f ? f() : NULL;
}
EXP int getgrouplist(const char*user, u32 gid, u32*groups, int*ngroups) {
    static int(*f)(const char*, u32, u32*, int*);
    if (!g_rl) {
        if (!f) f = nextsym("getgrouplist");
        return f ? f(user, gid, groups, ngroups) : -1;
    }
    u32 tmp[NVG];
    int c = cr_collect(user, gid, tmp, NVG);
    int room = *ngroups;
    *ngroups = c;
    for (int i = 0; i<c && i<room; i++) groups[i] = tmp[i];
    return room<c ? -1 : c;
}
/* getservby*() and getprotoby*() read /etc/services and /etc/protocols through
   glibc-internal calls, so they would see the host's files (Android has none).
   Parse the guest's files here; fall back to glibc when there is no match. */
struct prx_servent { char*name; char**aliases; int port; char*proto; };
struct prx_protoent { char*name; char**aliases; int proto; };
static char sv_pool[1536], *sv_al[12];
static struct prx_servent sv_se;
static struct prx_protoent sv_pe;
static char*sv_dup(const char*s, size_t n, size_t*used) {
    if (*used+n+1 > sizeof sv_pool) return NULL;
    char*d = sv_pool+*used;
    memcpy(d, s, n);
    d[n] = 0;
    *used += n+1;
    return d;
}
static int sv_num(const char*s, size_t n, int*out) {
    if (!n || n>9) return 0;
    int v = 0;
    for (size_t i = 0; i<n; i++) {
        if (s[i]<'0' || s[i]>'9') return 0;
        v = v*10+(s[i]-'0');
    }
    *out = v;
    return 1;
}
/* mode: 0 service by name, 1 service by port (host order), 2 protocol by name,
   3 protocol by number. Returns 1 and fills sv_se / sv_pe on a match. */
static int sv_find(int mode, const char*name, int num, const char*proto) {
    long n;
    char*buf = read_rel_all(mode<2 ? "/etc/services" : "/etc/protocols", &n);
    if (!buf) return 0;
    int found = 0;
    for (char*l = buf; *l && !found; ) {
        char*e = l;
        while (*e && *e != '\n') e++;
        char sv = *e;
        *e = 0;
        char*t[12];
        size_t tl[12];
        int k = 0;
        for (char*c = l; *c && *c != '#' && k<12; ) {
            while (*c == ' ' || *c == '\t' || *c == '\r') c++;
            if (!*c || *c == '#') break;
            t[k] = c;
            while (*c && *c != ' ' && *c != '\t' && *c != '\r' && *c != '#') c++;
            tl[k] = (size_t)(c-t[k]);
            k++;
        }
        if (k >= 2) {
            int port = 0, ok = 0;
            size_t pl = tl[1];
            const char*pr = NULL;
            size_t prl = 0;
            if (mode<2) {
                size_t i = 0;
                while (i<pl && t[1][i] != '/') i++;
                if (i<pl && sv_num(t[1], i, &port)) {
                    pr = t[1]+i+1;
                    prl = pl-i-1;
                    ok = 1;
                }
                if (ok && proto && (strlen(proto) != prl || memcmp(proto, pr, prl) != 0)) ok = 0;
            } else ok = sv_num(t[1], pl, &port);
            if (ok) {
                int hit = 0;
                if (mode == 1 || mode == 3) hit = (port == num);
                else for (int i = 0; i<k && !hit; i++) {
                    if (i == 1) continue;
                    hit = (strlen(name) == tl[i] && memcmp(name, t[i], tl[i]) == 0);
                }
                if (hit) {
                    size_t used = 0;
                    char*nm = sv_dup(t[0], tl[0], &used);
                    char*pp = mode<2 ? sv_dup(pr, prl, &used) : NULL;
                    int na = 0, bad = !nm || (mode<2 && !pp);
                    for (int i = 2; i<k && !bad; i++) {
                        char*a = sv_dup(t[i], tl[i], &used);
                        if (!a) bad = 1;
                        else sv_al[na++] = a;
                    }
                    if (!bad) {
                        sv_al[na] = NULL;
                        if (mode<2) {
                            sv_se.name = nm;
                            sv_se.aliases = sv_al;
                            sv_se.port = (int)(((port&0xff)<<8) | ((port>>8)&0xff));
                            sv_se.proto = pp;
                        } else {
                            sv_pe.name = nm;
                            sv_pe.aliases = sv_al;
                            sv_pe.proto = port;
                        }
                        found = 1;
                    }
                }
            }
        }
        *e = sv;
        l = *e ? e+1 : e;
    }
    free(buf);
    return found;
}
EXP void*getservbyname(const char*name, const char*proto) {
    static void*(*real)(const char*, const char*);
    if (name && g_rl && sv_find(0, name, 0, proto)) return &sv_se;
    if (!real) real = nextsym("getservbyname");
    return real(name, proto);
}
EXP void*getservbyport(int port, const char*proto) {
    static void*(*real)(int, const char*);
    int hp = ((port&0xff)<<8) | ((port>>8)&0xff);
    if (g_rl && sv_find(1, 0, hp&0xffff, proto)) return &sv_se;
    if (!real) real = nextsym("getservbyport");
    return real(port, proto);
}
EXP void*getprotobyname(const char*name) {
    static void*(*real)(const char*);
    if (name && g_rl && sv_find(2, name, 0, 0)) return &sv_pe;
    if (!real) real = nextsym("getprotobyname");
    return real(name);
}
EXP void*getprotobynumber(int num) {
    static void*(*real)(int);
    if (g_rl && sv_find(3, 0, num, 0)) return &sv_pe;
    if (!real) real = nextsym("getprotobynumber");
    return real(num);
}
