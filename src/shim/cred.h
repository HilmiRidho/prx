/* cred.h - setuid family, setfsuid, getresuid, groups (virtual credentials)
 * Part of the prx shim (unity build: included by src/prx.c, not a standalone header). */
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
#define IDFN0(name,ty,rn,ix) EXP ty name(void){static ty(*f)(void);if(g_fake)return 0;if(g_user)return g_cr[ix];if(!f)f=nextsym(rn);return f();}
IDFN0(getuid, u32, "getuid", C_RU) IDFN0(geteuid, u32, "geteuid", C_EU) IDFN0(getgid, u32, "getgid", C_RG) IDFN0(getegid, u32, "getegid", C_EG)
static int cr_setres(int b, u32 r, u32 e, u32 s) {
    int priv = g_cr[C_EU] == 0;
    u32*c = g_cr+b;
    if (!priv) {
        if (r != CR_NA && r != c[0] && r != c[1] && r != c[2]) return (int) fail(EPERM);
        if (e != CR_NA && e != c[0] && e != c[1] && e != c[2]) return (int) fail(EPERM);
        if (s != CR_NA && s != c[0] && s != c[1] && s != c[2]) return (int) fail(EPERM);
    }
    if (r != CR_NA) c[0] = r;
    if (e != CR_NA) c[1] = e;
    if (s != CR_NA) c[2] = s;
    c[3] = c[1];
    return 0;
}
static int cr_setre(int b, u32 r, u32 e) {
    int priv = g_cr[C_EU] == 0;
    u32*c = g_cr+b;
    u32 ro = c[0];
    if (!priv) {
        if (r != CR_NA && r != c[0] && r != c[1]) return (int) fail(EPERM);
        if (e != CR_NA && e != c[0] && e != c[1] && e != c[2]) return (int) fail(EPERM);
    }
    if (r != CR_NA) c[0] = r;
    if (e != CR_NA) c[1] = e;
    if (r != CR_NA || (e != CR_NA && e != ro)) c[2] = c[1];
    c[3] = c[1];
    return 0;
}
static int cr_setone(int b, u32 v) {
    u32*c = g_cr+b;
    if (g_cr[C_EU] == 0) return cr_setres(b, v, v, v);
    if (v != c[0] && v != c[2]) return (int) fail(EPERM);
    return cr_setres(b, CR_NA, v, CR_NA);
}
EXP int setuid(u32 a) {
    static int(*f)(u32);
    if (g_fake) return 0;
    if (g_user) return cr_setone(0, a);
    if (!f) f = nextsym("setuid");
    return f(a);
}
EXP int setgid(u32 a) {
    static int(*f)(u32);
    if (g_fake) return 0;
    if (g_user) return cr_setone(4, a);
    if (!f) f = nextsym("setgid");
    return f(a);
}
EXP int seteuid(u32 a) {
    static int(*f)(u32);
    if (g_fake) return 0;
    if (g_user) return cr_setres(0, CR_NA, a, CR_NA);
    if (!f) f = nextsym("seteuid");
    return f(a);
}
EXP int setegid(u32 a) {
    static int(*f)(u32);
    if (g_fake) return 0;
    if (g_user) return cr_setres(4, CR_NA, a, CR_NA);
    if (!f) f = nextsym("setegid");
    return f(a);
}
EXP int setreuid(u32 a, u32 b) {
    static int(*f)(u32, u32);
    if (g_fake) return 0;
    if (g_user) return cr_setre(0, a, b);
    if (!f) f = nextsym("setreuid");
    return f(a, b);
}
EXP int setregid(u32 a, u32 b) {
    static int(*f)(u32, u32);
    if (g_fake) return 0;
    if (g_user) return cr_setre(4, a, b);
    if (!f) f = nextsym("setregid");
    return f(a, b);
}
EXP int setresuid(u32 a, u32 b, u32 c) {
    static int(*f)(u32, u32, u32);
    if (g_fake) return 0;
    if (g_user) return cr_setres(0, a, b, c);
    if (!f) f = nextsym("setresuid");
    return f(a, b, c);
}
EXP int setresgid(u32 a, u32 b, u32 c) {
    static int(*f)(u32, u32, u32);
    if (g_fake) return 0;
    if (g_user) return cr_setres(4, a, b, c);
    if (!f) f = nextsym("setresgid");
    return f(a, b, c);
}
static u32 g_fake_groups[NVG] = {0};
static int g_fake_ngroups = 1;
EXP int getgroups(int size, u32*list) {
    if (g_user) {
        if (size == 0) return g_nvg;
        if (size<g_nvg) {
            *__errno_location() = EINVAL;
            return-1;
        }
        for (int i = 0; i<g_nvg; i++) list[i] = g_vg[i];
        return g_nvg;
    }
    if (g_fake) {
        if (size == 0) return g_fake_ngroups;
        if (size<g_fake_ngroups) {
            *__errno_location() = EINVAL;
            return-1;
        }
        for (int i = 0; i<g_fake_ngroups; i++) list[i] = g_fake_groups[i];
        return g_fake_ngroups;
    }
    static int(*f)(int, u32*);
    if (!f) f = nextsym("getgroups");
    return f ? f(size, list) : 0;
}
EXP int setgroups(size_t n, const void*l) {
    if (g_user) {
        if (g_cr[C_EU] != 0) return (int) fail(EPERM);
        if (n>NVG) return (int) fail(EINVAL);
        for (size_t i = 0; i<n; i++) g_vg[i] = ((const u32*) l)[i];
        g_nvg = (int) n;
        return 0;
    }
    if (g_fake) {
        if (n>NVG) return (int) fail(EINVAL);
        for (size_t i = 0; i<n; i++) g_fake_groups[i] = ((const u32*) l)[i];
        g_fake_ngroups = (int) n;
        return 0;
    }
    static int(*f)(size_t, const void*);
    if (!f) f = nextsym("setgroups");
    return f(n, l);
}
EXP int initgroups(const char*u, u32 g) {
    if (g_user) {
        if (g_cr[C_EU] != 0) return (int) fail(EPERM);
        g_nvg = cr_collect(u, g, g_vg, NVG);
        return 0;
    }
    if (g_fake) {
        g_fake_groups[0] = g;
        g_fake_ngroups = 1;
        return 0;
    }
    static int(*f)(const char*, u32);
    if (!f) f = nextsym("initgroups");
    return f(u, g);
}
EXP int getresuid(u32*ruid, u32*euid, u32*suid) {
    if (g_user) {
        if (ruid)*ruid = g_cr[C_RU];
        if (euid)*euid = g_cr[C_EU];
        if (suid)*suid = g_cr[C_SU];
        return 0;
    }
    if (g_fake) {
        if (ruid)*ruid = 0;
        if (euid)*euid = 0;
        if (suid)*suid = 0;
        return 0;
    }
    static int(*f)(u32*, u32*, u32*);
    if (!f) f = nextsym("getresuid");
    return f ? f(ruid, euid, suid) : 0;
}
EXP int getresgid(u32*rgid, u32*egid, u32*sgid) {
    if (g_user) {
        if (rgid)*rgid = g_cr[C_RG];
        if (egid)*egid = g_cr[C_EG];
        if (sgid)*sgid = g_cr[C_SG];
        return 0;
    }
    if (g_fake) {
        if (rgid)*rgid = 0;
        if (egid)*egid = 0;
        if (sgid)*sgid = 0;
        return 0;
    }
    static int(*f)(u32*, u32*, u32*);
    if (!f) f = nextsym("getresgid");
    return f ? f(rgid, egid, sgid) : 0;
}
