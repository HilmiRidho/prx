/* Path-translation parity test: the shim (prx.c) and the ptrace supervisor
 * (prxsup.c) must translate every path identically.
 *   cc -w -DSHIM -o xlate_shim xlate_cmp.c       cc -w -DSUP -o xlate_sup xlate_cmp.c
 * Both read the same environment (PRX_ROOTFS, PRX_BIND, T = scratch dir with the
 * layout built by tests/ptrace.sh) and print one line per (cwd, path, follow). */
#if defined(SHIM)
#include "../src/prx.c"
extern int printf(const char*, ...);
extern int snprintf(char*, size_t, const char*, ...);
static int xl(const char *p, int follow, char *buf, const char **out) {
    if (tr(AT_FDCWD, p, buf, follow, out) < 0) return -geterr();
    return 0;
}
static int enter(const char *dir) { return (int) sc(N_chdir, (long) dir, 0, 0, 0, 0, 0); }
#else
#define main sup_main
#include "../src/prxsup.c"
#undef main
static int xl(const char *p, int follow, char *buf, const char **out) {
    return x_tr(getpid(), AT_FDCWD, p, buf, follow, out);
}
static int enter(const char *dir) { return (int) syscall(SYS_chdir, dir); }
#endif

int main(void) {
    static const char *const paths[] = {
        "/etc/passwd", "/etc/../etc/./passwd", "//etc///passwd", "/usr/../../etc/passwd", "/..", "/", "",
        "lnk-abs", "lnk-abs/passwd", "lnk-rel/passwd", "/lnk-abs", "/lnk-abs/", "/lnk-rel/../etc",
        "loop1", "loop1/x", "/missing/dir/file", "/usr/bin/sh", "etc", "etc/passwd", "./etc/passwd", "../etc/passwd",
        "../../..", "passwd", "x/y/../z", "/proc/self/status", "/sys/kernel", "/dev/null", "/dev/shm/x",
        "/data", "/data/", "/data/f", "/data/../etc/passwd", "/data/sub/../f", "/data/lnk", "/data/lnk/f",
        "/data/hostlnk", "/data/hostlnk/f", "ident/f",
    };
    static const char *const cwds[] = { "R", "R/etc", "R/usr/bin", "H", "H/sub", "ID", "/" };
    const char *t = getenv("T");
    char idpath[4096], hpath[4096], cwd[4096], buf[4096], full[4096];
    unsigned c, i, k;
    if (!t) return 2;
#ifdef SUP
    if (!cfg_load()) return 3;
#endif
    snprintf(idpath, sizeof idpath, "%s/hostid", t);
    snprintf(hpath, sizeof hpath, "%s/hostdata", t);
    for (c = 0; c < sizeof cwds / sizeof cwds[0]; c++) {
        const char *w = cwds[c];
        if (!strcmp(w, "/")) snprintf(cwd, sizeof cwd, "/");
        else if (!strcmp(w, "H")) snprintf(cwd, sizeof cwd, "%s", hpath);
        else if (!strcmp(w, "H/sub")) snprintf(cwd, sizeof cwd, "%s/sub", hpath);
        else if (!strcmp(w, "ID")) snprintf(cwd, sizeof cwd, "%s", idpath);
        else snprintf(cwd, sizeof cwd, "%s/root%s", t, w + 1);
        if (enter(cwd) < 0) { printf("# cannot enter %s\n", cwd); continue; }
        for (i = 0; i < sizeof paths / sizeof paths[0]; i++) {
            for (k = 0; k < 2; k++) {
                const char *out = NULL;
                int e;
                snprintf(full, sizeof full, "%s", paths[i]);
                if (!strcmp(paths[i], "ident/f")) snprintf(full, sizeof full, "%s/f", idpath);
                e = xl(full, (int) k, buf, &out);
                /* print paths relative to the scratch dir so both runs agree */
                if (e < 0) printf("%s|%s|%u -> ERR %d\n", w, full, k, -e);
                else printf("%s|%s|%u -> %s\n", w, full, k, out == full ? "(same)" : out);
            }
        }
    }
    return 0;
}
