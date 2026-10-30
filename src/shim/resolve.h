/* resolve.h - IPv4/IPv6 parsing, /etc/hosts, DNS, getaddrinfo
 * Part of the prx shim (unity build: included by src/prx.c, not a standalone header). */
static int parse_v4(const char*s, size_t n, u8 out[4]) {
    size_t i = 0;
    for (int part = 0; part<4; part++) {
        unsigned v = 0, d = 0;
        while (i<n && s[i] >= '0' && s[i] <= '9') {
            v = v*10+(unsigned)(s[i++]-'0');
            d++;
            if (v>255) return 0;
        }
        if (!d) return 0;
        out[part] = (u8) v;
        if (part<3) {
            if (i >= n || s[i] != '.') return 0;
            i++;
        }
    }
    return i == n;
}
/* IPv6 literal (RFC 4291 text form, incl. "::" and an embedded dotted quad).
 * Zone ids ("%eth0") are not accepted. */
static int parse_v6(const char*s, size_t n, u8 out[16]) {
    u16 w[8];
    int nw = 0, gap = -1;
    size_t i = 0;
    if (!n || n>45) return 0;
    if (n >= 2 && s[0] == ':' && s[1] == ':') {
        gap = 0;
        i = 2;
        if (i == n) {
            memset(out, 0, 16);
            return 1;
        }
    } else if (s[0] == ':') return 0;
    while (i<n) {
        size_t st = i;
        unsigned v = 0;
        int d = 0;
        while (i<n && ((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') || (s[i] >= 'A' && s[i] <= 'F'))) {
            char c = s[i++];
            v = v*16+(unsigned)(c <= '9' ? c-'0' : (c|32)-'a'+10);
            if (++d>4) return 0;
        }
        if (i<n && s[i] == '.') {
            u8 q[4];
            if (nw>6 || !parse_v4(s+st, n-st, q)) return 0;
            w[nw++] = (u16)((q[0]<<8)|q[1]);
            w[nw++] = (u16)((q[2]<<8)|q[3]);
            i = n;
            break;
        }
        if (!d || nw >= 8) return 0;
        w[nw++] = (u16) v;
        if (i == n) break;
        if (s[i] != ':') return 0;
        i++;
        if (i<n && s[i] == ':') {
            if (gap >= 0) return 0;
            gap = nw;
            i++;
            if (i == n) break;
        } else if (i == n) return 0;
    }
    if (gap < 0 ? nw != 8 : nw >= 8) return 0;
    int z = 8-nw;
    int o = 0;
    for (int k = 0; k<8; k++) {
        u16 v;
        if (gap >= 0 && k >= gap && k<gap+z) v = 0;
        else v = w[o++];
        out[2*k] = (u8)(v>>8);
        out[2*k+1] = (u8) v;
    }
    return 1;
}
static void fmt_v4(const u8 ip[4], char*o) {
    for (int i = 0; i<4; i++) {
        unsigned v = ip[i];
        if (v >= 100)*o++ = (char)('0'+v/100);
        if (v >= 10)*o++ = (char)('0'+(v/10)%10);
        *o++ = (char)('0'+v%10);
        if (i<3)*o++ = '.';
    }
    *o = 0;
}
static int hosts_lookup(const char*name, u8 ip[4]) {
    long n;
    char*buf = read_rel_all("/etc/hosts", &n);
    if (!buf) return 0;
    if (n <= 0) {
        free(buf);
        return 0;
    }
    size_t nl = strlen(name);
    char*p = buf;
    int found = 0;
    while (*p) {
        char*eol = p;
        while (*eol && *eol != '\n') eol++;
        char*c = p;
        while (c<eol && (*c == ' ' || *c == '\t')) c++;
        if (c<eol && *c != '#') {
            char*t = c;
            while (t<eol && *t != ' ' && *t != '\t') t++;
            u8 a[4];
            if (parse_v4(c, (size_t)(t-c), a)) {
                while (t<eol && *t != '#') {
                    while (t<eol && (*t == ' ' || *t == '\t')) t++;
                    char*w = t;
                    while (t<eol && *t != ' ' && *t != '\t' && *t != '#') t++;
                    if ((size_t)(t-w) == nl && !memcmp(w, name, nl)) {
                        memcpy(ip, a, 4);
                        found = 1;
                        break;
                    }
                }
            }
        }
        if (found) break;
        p = *eol ? eol+1 : eol;
    }
    free(buf);
    return found;
}
/* Ask one server (IPv4 or IPv6) for an A record. Returns 1 and fills ip on
 * success, 0 otherwise. */
static int dns_ask(int fam, const u8*saddr, const u8*q, size_t k, const u8 id[2], u8 ip[4]) {
    int s = socket(fam, 2, 0);
    if (s < 0) return 0;
    int found = 0;
    struct {
        u16 fam;
        u16 port;
        u32 flow;
        u8 addr[16];
        u32 scope;
    } sa6;
    struct {
        u16 fam;
        u16 port;
        u8 addr[4];
        u8 z[8];
    } sa4;
    const void*sa;
    unsigned sl;
    u16 port = (u16)((53>>8)|((53&0xff)<<8));
    if (fam == 10) {
        memset(&sa6, 0, sizeof sa6);
        sa6.fam = 10;
        sa6.port = port;
        memcpy(sa6.addr, saddr, 16);
        sa = &sa6;
        sl = sizeof sa6;
    } else {
        memset(&sa4, 0, sizeof sa4);
        sa4.fam = 2;
        sa4.port = port;
        memcpy(sa4.addr, saddr, 4);
        sa = &sa4;
        sl = sizeof sa4;
    }
    long tv[2] = {2, 0};
    setsockopt(s, 1, 20, tv, sizeof tv);
    if (connect(s, sa, sl) == 0) {
        for (int tries = 0; tries<2 && !found; tries++) {
            if (send(s, q, k, 0)<0) break;
            u8 r[1500];
            long rn = recv(s, r, sizeof r, 0);
            if (rn<12 || r[0] != id[0] || r[1] != id[1]) continue;
            if ((r[3]&0x0f) != 0) break;
            int an = (r[6]<<8)|r[7];
            long o = 12;
            int qd = (r[4]<<8)|r[5];
            for (int i = 0; i<qd && o<rn; i++) {
                while (o<rn && r[o]) {
                    if ((r[o]&0xc0) == 0xc0) {
                        o++;
                        break;
                    }
                    o += r[o]+1;
                }
                o += 1+4;
            }
            for (int i = 0; i<an && o<rn; i++) {
                while (o<rn && r[o]) {
                    if ((r[o]&0xc0) == 0xc0) {
                        o++;
                        break;
                    }
                    o += r[o]+1;
                }
                o++;
                if (o+10>rn) break;
                int ty = (r[o]<<8)|r[o+1];
                int rl = (r[o+8]<<8)|r[o+9];
                o += 10;
                if (ty == 1 && rl == 4 && o+4 <= rn) {
                    memcpy(ip, r+o, 4);
                    found = 1;
                    break;
                }
                o += rl;
            }
            break;
        }
    }
    sc(N_close, s, 0, 0, 0, 0, 0);
    return found;
}
static int dns_query(const char*name, u8 ip[4]) {
    u8 q[300];
    size_t nl = strlen(name);
    if (!nl || nl>253) return 0;
    u8 id[2];
    sc(N_getrandom, (long) id, 2, 0, 0, 0, 0);
    size_t k = 0;
    q[k++] = id[0];
    q[k++] = id[1];
    q[k++] = 0x01;
    q[k++] = 0x00;
    q[k++] = 0;
    q[k++] = 1;
    for (int i = 0; i<6; i++) q[k++] = 0;
    const char*p = name;
    while (*p) {
        const char*e = p;
        while (*e && *e != '.') e++;
        size_t l = (size_t)(e-p);
        if (!l || l>63) return 0;
        q[k++] = (u8) l;
        memcpy(q+k, p, l);
        k += l;
        p = *e ? e+1 : e;
    }
    q[k++] = 0;
    q[k++] = 0;
    q[k++] = 1;
    q[k++] = 0;
    q[k++] = 1;
    /* g_dns: comma-separated IPv4 / IPv6 literals ("[addr]" is accepted too) */
    const char*sv = g_dns;
    while (*sv) {
        const char*e = sv;
        while (*e && *e != ',') e++;
        const char*a = sv;
        size_t al = (size_t)(e-sv);
        if (al >= 2 && a[0] == '[' && a[al-1] == ']') {
            a++;
            al -= 2;
        }
        u8 sip[16];
        if (parse_v4(a, al, sip)) {
            if (dns_ask(2, sip, q, k, id, ip)) return 1;
        } else if (parse_v6(a, al, sip)) {
            if (dns_ask(10, sip, q, k, id, ip)) return 1;
        }
        sv = *e ? e+1 : e;
    }
    return 0;
}
static int resolve_name(const char*name, char*ipstr) {
    u8 ip[4];
    if (hosts_lookup(name, ip) || dns_query(name, ip)) {
        fmt_v4(ip, ipstr);
        return 1;
    }
    return 0;
}
static int is_numeric(const char*n) {
    u8 t[4];
    if (parse_v4(n, strlen(n), t)) return 1;
    for (const char*c = n; *c; c++) if (*c == ':') return 1;
    return 0;
}
struct ai_hints {
    int flags, family, socktype, protocol;
};
#define AI_NUMERICHOST 4
EXP int getaddrinfo(const char*node, const char*service, const void*hints, void**res) {
    static int(*real)(const char*, const char*, const void*, void**);
    if (!real) real = nextsym("getaddrinfo");
    int r = real(node, service, hints, res);
    if (r == 0 || !g_rl || !node || is_numeric(node)) return r;
    const struct ai_hints*h = hints;
    if (h && (h->flags&AI_NUMERICHOST)) return r;
    if (h && h->family == 10) return r;
    char ip[32];
    if (!resolve_name(node, ip)) return r;
    struct ai_hints nh = {0, 0, 0, 0};
    if (h) nh = *h;
    nh.flags |= AI_NUMERICHOST;
    char hb[64];
    memset(hb, 0, sizeof hb);
    memcpy(hb, &nh, sizeof nh);
    return real(ip, service, hb, res);
}
EXP void*gethostbyname(const char*name) {
    static void*(*real)(const char*);
    if (!real) real = nextsym("gethostbyname");
    void*r = real(name);
    if (r || !g_rl || !name || is_numeric(name)) return r;
    char ip[32];
    if (resolve_name(name, ip)) return real(ip);
    return r;
}
