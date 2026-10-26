/* prx_misc.h - capabilities, prctl, uname, signals and other small hooks
 *
 * capget/capset, prctl, uname, sigaction/signal, sigprocmask, socket,
 * lckpwdf, is_selinux_enabled, setfsuid/setfsgid and sysinfo.
 *
 * Part of the shim. Included only by prx.c, in the order listed there:
 * the shim is one translation unit and later parts use what earlier
 * parts define. Do not compile this file on its own.
 */
#ifndef PRX_MISC_H
#define PRX_MISC_H

struct cap_header_t {
    u32 version;
    int pid;
};
struct cap_data_t {
    u32 effective;
    u32 permitted;
    u32 inheritable;
};
EXP int capget(struct cap_header_t*hdrp, struct cap_data_t*datap) {
    if (g_user && g_cr[C_EU] != 0) {
        if (datap) {
            memset(datap, 0, sizeof (struct cap_data_t));
            if (hdrp && hdrp->version == 0x20080522) memset(datap+1, 0, sizeof (struct cap_data_t));
        }
        return 0;
    }
    if (VROOT()) {
        if (datap) {
            datap[0].effective = 0xffffffff;
            datap[0].permitted = 0xffffffff;
            datap[0].inheritable = 0xffffffff;
            if (hdrp && hdrp->version == 0x20080522) {
                datap[1].effective = 0xffffffff;
                datap[1].permitted = 0xffffffff;
                datap[1].inheritable = 0xffffffff;
            }
        }
        return 0;
    }
    static int(*f)(struct cap_header_t*, struct cap_data_t*);
    if (!f) f = nextsym("capget");
    return f ? f(hdrp, datap) : 0;
}
EXP int capset(struct cap_header_t*hdrp, const struct cap_data_t*datap) {
    if (g_fake || g_user) return 0;
    static int(*f)(struct cap_header_t*, const struct cap_data_t*);
    if (!f) f = nextsym("capset");
    return f ? f(hdrp, datap) : 0;
}
EXP int prctl(int option, unsigned long a2, unsigned long a3, unsigned long a4, unsigned long a5) {
    if ((g_fake || g_user) && option == 23) return 1;
    if (option == 38) return 0;
    static int(*f)(int, unsigned long, unsigned long, unsigned long, unsigned long);
    if (!f) f = nextsym("prctl");
    return f ? f(option, a2, a3, a4, a5) : 0;
}
struct utsname_t {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};
EXP int uname(struct utsname_t*buf) {
    static int(*f)(struct utsname_t*);
    if (!f) f = nextsym("uname");
    int r = f ? f(buf) : 0;
    if (r == 0 && buf) {
        memcpy(buf->sysname, "Linux", 6);
        memcpy(buf->release, "5.15.0-prx", 11);
        memcpy(buf->version, "#1 SMP PREEMPT", 15);
    }
    return r;
}
EXP int sigaction(int sig, const void*act, void*old) {
    static int(*f)(int, const void*, void*);
    if (!f) f = nextsym("sigaction");
    if (sig == 31 && g_rl && !g_inst) {
        if (old) memset(old, 0, sizeof (struct ksa));
        return 0;
    }
    return f(sig, act, old);
}
EXP void*signal(int sig, void*h) {
    static void*(*f)(int, void*);
    if (!f) f = nextsym("signal");
    if (sig == 31 && g_rl) return NULL;
    return f(sig, h);
}
EXP void*bsd_signal(int sig, void*h) {
    static void*(*f)(int, void*);
    if (!f) f = nextsym("bsd_signal");
    if (sig == 31 && g_rl) return NULL;
    return f(sig, h);
}
EXP int socket(int dom, int type, int proto) {
    static int(*f)(int, int, int);
    if (g_rl && dom == 16 && proto == 9) return (int) fail(93);
    if (!f) f = nextsym("socket");
    return f(dom, type, proto);
}
EXP int lckpwdf(void) {
    static int(*f)(void);
    if (g_rl) return 0;
    if (!f) f = nextsym("lckpwdf");
    return f();
}
EXP int ulckpwdf(void) {
    static int(*f)(void);
    if (g_rl) return 0;
    if (!f) f = nextsym("ulckpwdf");
    return f();
}
EXP int is_selinux_enabled(void) {
    static int(*f)(void);
    if (g_rl) return 0;
    if (!f) f = nextsym("is_selinux_enabled");
    return f ? f() : 0;
}
static const void*unblock_sys(const void*set, int how, u8*tmp) {
    if (!g_rl || !set || (how != 0 && how != 2)) return set;
    memcpy(tmp, set, 128);
    tmp[3] &= (u8)~(1u<<6);
    return tmp;
}
EXP int sigprocmask(int how, const void*set, void*old) {
    static int(*f)(int, const void*, void*);
    u8 t[128];
    if (!f) f = nextsym("sigprocmask");
    return f(how, unblock_sys(set, how, t), old);
}
EXP int pthread_sigmask(int how, const void*set, void*old) {
    static int(*f)(int, const void*, void*);
    u8 t[128];
    if (!f) f = nextsym("pthread_sigmask");
    return f(how, unblock_sys(set, how, t), old);
}
EXP int setfsuid(u32 u) {
    static int(*f)(u32);
    if (g_fake) return 0;
    if (g_user) {
        u32 o = g_cr[C_FU];
        if (g_cr[C_EU] == 0 || u == g_cr[C_RU] || u == g_cr[C_EU] || u == g_cr[C_SU] || u == g_cr[C_FU]) g_cr[C_FU] = u;
        return (int) o;
    }
    if (!f) f = nextsym("setfsuid");
    return f(u);
}
EXP int setfsgid(u32 g) {
    static int(*f)(u32);
    if (g_fake) return 0;
    if (g_user) {
        u32 o = g_cr[C_FG];
        if (g_cr[C_EU] == 0 || g == g_cr[C_RG] || g == g_cr[C_EG] || g == g_cr[C_SG] || g == g_cr[C_FG]) g_cr[C_FG] = g;
        return (int) o;
    }
    if (!f) f = nextsym("setfsgid");
    return f(g);
}
struct sysinfo_data {
    long uptime;
    unsigned long loads[3];
    unsigned long totalram;
    unsigned long freeram;
    unsigned long sharedram;
    unsigned long bufferram;
    unsigned long totalswap;
    unsigned long freeswap;
    unsigned short procs;
    unsigned short pad;
    unsigned long totalhigh;
    unsigned long freehigh;
    unsigned int mem_unit;
    char _f[20-2*sizeof (long)-sizeof (int)];
};
EXP int sysinfo(struct sysinfo_data*info) {
    static int(*real_sysinfo)(struct sysinfo_data*);
    if (!real_sysinfo) real_sysinfo = nextsym("sysinfo");
    int res = -1;
    if (real_sysinfo) res = real_sysinfo(info);
    if (info && (res != 0 || info->uptime <= 0)) {
        info->uptime = 43200;
        info->loads[0] = 32768;
        info->loads[1] = 49152;
        info->loads[2] = 52428;
        info->procs = 25;
        return 0;
    }
    return res;
}

#endif /* PRX_MISC_H */
