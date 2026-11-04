/* procfs.h - fake /proc files and mountinfo emulation
 * Part of the prx shim (unity build: included by src/prx.c, not a standalone header). */
static int fake_proc_file(const char*content, size_t len) {
    long fd = sc(N_memfd_create, (long) "fake_proc", 1, 0, 0, 0, 0);
    if (fd<0) {
        int pfd[2];
        if (sc(N_pipe2, (long) pfd, O_CLOEXEC, 0, 0, 0, 0) == 0) {
            sc(N_write, pfd[1], (long) content, (long) len, 0, 0, 0);
            sc(N_close, pfd[1], 0, 0, 0, 0, 0);
            return pfd[0];
        }
        return-1;
    }
    sc(N_write, fd, (long) content, (long) len, 0, 0, 0);
    sc(N_lseek, fd, 0, 0, 0, 0, 0);
    return (int) fd;
}
/* /proc/stat for the guest. libuv (os.cpus(), hence Node.js and npm) counts the
   CPUs from the per-CPU "cpuN" lines, so a file with only the aggregate "cpu"
   line made os.cpus() return [] and npm hang with "Exit handler never called".
   One line per CPU in the affinity mask; the counters are fixed, as before. */
static size_t stat_put_u(char*d, u64 v) {
    char t[24];
    int k = 0;
    size_t o = 0;
    do {
        t[k++] = (char)('0'+(int)(v%10));
        v /= 10;
    } while (v);
    while (k) d[o++] = t[--k];
    return o;
}
static int fake_proc_stat(void) {
    static const u32 tot[10] = {21200, 450, 12300, 954000, 3200, 120, 80, 0, 0, 0};
    static const char tail[] = "intr 1234567 10 20 0 0 0\nctxt 5678901\nbtime 1680000000\n"
                               "processes 25000\nprocs_running 1\nprocs_blocked 0\n";
    u64 mask[16];
    char buf[16384];
    int n = 0, i, j, k;
    size_t o = 0;
    memset(mask, 0, sizeof mask);
    if (sched_getaffinity(0, sizeof mask, mask) == 0) {
        for (i = 0; i<16; i++)
            for (u64 w = mask[i]; w; w >>= 1) n += (int)(w&1);
    }
    if (n >= 1024 || n<1) n = 1;    /* all-ones = synthetic mask: count unknown */
    if (n>256) n = 256;
    for (j = -1; j<n; j++) {
        memcpy(buf+o, "cpu", 3);
        o += 3;
        if (j >= 0) o += stat_put_u(buf+o, (u64) j);
        else buf[o++] = ' ';
        for (k = 0; k<10; k++) {
            buf[o++] = ' ';
            o += stat_put_u(buf+o, j<0 ? (u64) tot[k] : (u64) tot[k]/(u64) n);
        }
        buf[o++] = '\n';
    }
    memcpy(buf+o, tail, sizeof (tail)-1);
    o += sizeof (tail)-1;
    return fake_proc_file(buf, o);
}
struct mi {
    char maj[24], opts[256], fst[64], src[256], sup[256];
    int ok;
    size_t len;
};
static void mi_cp(char*d, size_t cap, const char*s) {
    size_t i = 0;
    while (s[i] && i+1<cap) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}
static void mi_set(struct mi*d, const char*maj, const char*o, const char*f, const char*sr, const char*su) {
    mi_cp(d->maj, sizeof d->maj, maj);
    mi_cp(d->opts, sizeof d->opts, o);
    mi_cp(d->fst, sizeof d->fst, f);
    mi_cp(d->src, sizeof d->src, sr);
    mi_cp(d->sup, sizeof d->sup, su);
    d->ok = 1;
}
static void mi_line(char*l, struct mi*r) {
    char*t[40];
    int k = 0, d = -1;
    char*p = l;
    while (*p && k<40) {
        while (*p == ' ')*p++ = 0;
        if (!*p) break;
        t[k++] = p;
        while (*p && *p != ' ') p++;
    }
    for (int i = 6; i<k; i++) if (t[i][0] == '-' && !t[i][1]) {
        d = i;
        break;
    }
    if (d<0 || d+2 >= k) return;
    const char*su = d+3<k ? t[d+3] : "";
    size_t ml = strlen(t[4]);
    if (ml && g_rl >= ml && strncmp(g_root, t[4], ml) == 0 && (ml == 1 || g_root[ml] == 0 || g_root[ml] == '/') && (!r[0].ok || ml >= r[0].len)) {
        mi_set(&r[0], t[2], t[5], t[d+1], t[d+2], su);
        r[0].len = ml;
    }
    if (strcmp(t[4], "/proc") == 0) mi_set(&r[1], t[2], t[5], t[d+1], t[d+2], su);
    if (strcmp(t[4], "/sys") == 0) mi_set(&r[2], t[2], t[5], t[d+1], t[d+2], su);
}
static void host_mounts(struct mi*r) {
    long fd = sc(N_openat, AT_FDCWD, (long) "/proc/self/mountinfo", O_RDONLY|O_CLOEXEC, 0, 0, 0);
    if (fd<0) return;
    char ck[2048], ln[PM];
    size_t ll = 0;
    long n;
    while ((n = sc(N_read, fd, (long) ck, sizeof ck, 0, 0, 0))>0) {
        for (long i = 0; i<n; i++) {
            if (ck[i] == '\n') {
                ln[ll] = 0;
                mi_line(ln, r);
                ll = 0;
            } else if (ll+1<sizeof ln) ln[ll++] = ck[i];
        }
    }
    sc(N_close, fd, 0, 0, 0, 0, 0);
}
static size_t mi_app(char*o, size_t n, size_t cap, const char*s) {
    size_t l = strlen(s);
    if (n+l+1 >= cap) return n;
    memcpy(o+n, s, l);
    return n+l;
}
static size_t build_mi(char*o, size_t cap, int info) {
    struct mi r[3];
    for (int i = 0; i<3; i++) {
        r[i].ok = 0;
        r[i].len = 0;
    }
    host_mounts(r);
    if (!r[0].ok) mi_set(&r[0], "0:1", "rw,relatime", "rootfs", "rootfs", "rw");
    if (!r[1].ok) mi_set(&r[1], "0:2", "rw,relatime", "proc", "proc", "rw");
    if (!r[2].ok) mi_set(&r[2], "0:3", "rw,relatime", "sysfs", "sysfs", "rw");
    static const char*const mp[3] = {"/", "/proc", "/sys"};
    size_t n = 0;
    for (int i = 0; i<3; i++) {
        const char*su = r[i].sup[0] ? r[i].sup : "rw";
        char id[3] = {(char)('1'+i), 0, 0};
        char par[2] = {i ? '1' : '0', 0};
        if (info) {
            n = mi_app(o, n, cap, id);
            n = mi_app(o, n, cap, " ");
            n = mi_app(o, n, cap, par);
            n = mi_app(o, n, cap, " ");
            n = mi_app(o, n, cap, r[i].maj);
            n = mi_app(o, n, cap, " / ");
            n = mi_app(o, n, cap, mp[i]);
            n = mi_app(o, n, cap, " ");
            n = mi_app(o, n, cap, r[i].opts);
            n = mi_app(o, n, cap, " - ");
            n = mi_app(o, n, cap, r[i].fst);
            n = mi_app(o, n, cap, " ");
            n = mi_app(o, n, cap, r[i].src);
            n = mi_app(o, n, cap, " ");
            n = mi_app(o, n, cap, su);
            n = mi_app(o, n, cap, "\n");
        } else {
            n = mi_app(o, n, cap, r[i].src);
            n = mi_app(o, n, cap, " ");
            n = mi_app(o, n, cap, mp[i]);
            n = mi_app(o, n, cap, " ");
            n = mi_app(o, n, cap, r[i].fst);
            n = mi_app(o, n, cap, " ");
            n = mi_app(o, n, cap, r[i].opts);
            const char*s2 = r[i].sup;
            if (s2[0] == 'r' && (s2[1] == 'w' || s2[1] == 'o') && s2[2] == ',') s2 += 3;
            if (s2[0] && strcmp(s2, "rw") && strcmp(s2, "ro")) {
                n = mi_app(o, n, cap, ",");
                n = mi_app(o, n, cap, s2);
            }
            n = mi_app(o, n, cap, " 0 0\n");
        }
    }
    return n;
}

/* ---- generic fallbacks for /proc entries the host denies (Android/Termux SELinux) ----
   Existence checks (stat/statx/access F_OK) on a denied /proc file are answered as
   "exists, empty regular file", so launchers that only probe /proc (LibreOffice's
   oosplash does stat("/proc/version")) stop failing. The real open()/read() is not
   changed by this: a program that then reads the file still gets the host's error.
   Per-process paths (/proc/<pid>/..., /proc/self/...) are never faked. */
static int proc_fakeable(const char*p) {
    if (!p || strncmp(p, "/proc/", 6) != 0 || !p[6]) return 0;
    if (p[6] >= '0' && p[6] <= '9') return 0;
    if (strncmp(p+6, "self/", 5) == 0 || strcmp(p+6, "self") == 0) return 0;
    if (strncmp(p+6, "thread-self", 11) == 0) return 0;
    return 1;
}
static int proc_denied(long r) {
    return r == -EACCES || r == -EPERM;
}
/* Contents served only when the host denies the real file. */
static int proc_fallback_open(const char*p, int fl) {
    static const struct {
        const char*path, *txt;
    } t[] = {
        {"/proc/sys/kernel/cap_last_cap", "40\n"},
        {"/proc/sys/fs/inotify/max_user_watches", "8192\n"},
        {"/proc/sys/fs/inotify/max_user_instances", "128\n"},
        {"/proc/sys/fs/inotify/max_queued_events", "16384\n"},
        {"/proc/vmstat",
         "nr_free_pages 100000\nnr_inactive_anon 20000\nnr_active_anon 30000\n"
         "nr_inactive_file 40000\nnr_active_file 50000\npgpgin 1000000\npgpgout 500000\n"
         "pswpin 0\npswpout 0\npgfault 2000000\npgmajfault 1000\n"},
    };
    if ((fl&O_ACCMODE_) != O_RDONLY) return -1;
    for (unsigned i = 0; i<sizeof t/sizeof t[0]; i++)
        if (strcmp(p, t[i].path) == 0) return fake_proc_file(t[i].txt, strlen(t[i].txt));
    return -1;
}
