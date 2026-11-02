/* init.h - bind parsing, constructor / early init
 * Part of the prx shim (unity build: included by src/prx.c, not a standalone header). */
static void addbind(const char*s, size_t n) {
    size_t eq = n, i;
    for (i = 0; i<n; i++) {
        if (s[i] == '=') {
            eq = i;
            break;
        }
    }
    size_t gn = eq, hs = eq<n ? eq+1 : 0, hn = eq<n ? n-eq-1 : n;
    const char*hp = eq<n ? s+hs : s;
    if (!gn || g_nb >= MAXB || gn >= sizeof (g_bind[0]) || hn >= sizeof (g_bhost[0]) || s[0] != '/' || hp[0] != '/') return;
    memcpy(g_bind[g_nb], s, gn);
    g_bind[g_nb][gn] = 0;
    while (gn>1 && g_bind[g_nb][gn-1] == '/') g_bind[g_nb][--gn] = 0;
    memcpy(g_bhost[g_nb], hp, hn);
    g_bhost[g_nb][hn] = 0;
    while (hn>1 && g_bhost[g_nb][hn-1] == '/') g_bhost[g_nb][--hn] = 0;
    g_bindlen[g_nb] = gn;
    g_bhlen[g_nb] = hn;
    g_bid[g_nb] = (gn == hn && !memcmp(g_bind[g_nb], g_bhost[g_nb], gn));
    g_nb++;
}
static int envmatch(const char*e, const char*k) {
    size_t n = strlen(k);
    return strncmp(e, k, n) == 0 && e[n] == '=';
}
static void copyval(char*dst, size_t cap, const char*src) {
    size_t n = strlen(src);
    if (n >= cap) n = cap-1;
    memcpy(dst, src, n);
    dst[n] = 0;
}
static void prx_early(void);
static int g_inited;
static void prx_do_init(char**envp) {
    if (g_inited) return;
    g_inited = 1;
    for (char**e = envp; e && *e; e++) {
        const char*s = *e;
        if (envmatch(s, "PRX_ROOTFS")) copyval(g_root, PM, s+11);
        else if (envmatch(s, "PRX_SHIM")) copyval(g_shim, PM, s+9);
        else if (envmatch(s, "PRX_LOADER")) copyval(g_loader, PM, s+11);
        else if (envmatch(s, "PRX_QEMU")) copyval(g_qemu, PM, s+9);
        else if (envmatch(s, "PRX_LIBPATH")) copyval(g_libpath, LPM, s+12);
        else if (envmatch(s, "PRX_BIND")) copyval(g_bindenv, sizeof g_bindenv, s+9);
        else if (envmatch(s, "PRX_DNS")) copyval(g_dns, sizeof g_dns, s+8);
        else if (envmatch(s, "PRX_SELF_EXE")) copyval(g_self_exe, PM, s+13);
        else if (envmatch(s, "PRX_NO_ARGV0")) g_noargv0 = (s[13] == '1');
        else if (envmatch(s, "PRX_SECCOMP")) g_seccomp = (s[12] != '0');
        else if (envmatch(s, "PRX_TRACE")) g_trace = (s[10] == '1');
        else if (envmatch(s, "PRX_FAKEROOT")) g_fake = (s[13] == '1');
        else if (envmatch(s, "PRX_VUSER")) copyval(g_vuser, sizeof g_vuser, s+10);
        else if (envmatch(s, "PRX_CRED")) copyval(g_credbuf, sizeof g_credbuf, s+9);
    }
    g_rl = strlen(g_root);
    while (g_rl>1 && g_root[g_rl-1] == '/') g_root[--g_rl] = 0;
    if (g_rl == 1) g_rl = 0;
    g_nb = 0;
    addbind("/proc", 5);
    addbind("/sys", 4);
    addbind("/dev/urandom", 12);
    addbind("/dev/random", 11);
    addbind("/dev/zero", 9);
    addbind("/dev/null", 9);
    addbind("/dev/full", 9);
    addbind("/dev/pts", 8);
    addbind("/dev/ptmx", 9);
    addbind("/dev/fd", 7);
    addbind("/dev/stdin", 10);
    addbind("/dev/stdout", 11);
    addbind("/dev/stderr", 11);
    addbind("/dev/tty", 8);
    const char*p = g_bindenv;
    while (*p) {
        const char*q = p;
        while (*q && *q != ':') q++;
        addbind(p, (size_t)(q-p));
        p = *q ? q+1 : q;
    }
    seccomp_init();
    g_ruid = (u32) sc(N_getuid, 0, 0, 0, 0, 0, 0);
    g_rgid = (u32) sc(N_getgid, 0, 0, 0, 0, 0, 0);
    g_user = (!g_fake && g_vuser[0] && g_rl) ? 1 : 0;
    if (g_user) cred_init();
}
__attribute__((constructor)) static void prx_init(int argc, char**argv, char**envp) {
    (void) argc;
    (void) argv;
    prx_do_init(envp);
}
/* LD_PRELOAD constructors run after the constructors of the app's own libs
   (jemalloc, ...). Interposed entry points call this so the SIGSYS handler
   and path translation are active before those constructors do real work. */
static void prx_early(void) {
    if (!g_inited) prx_do_init(environ);
}
