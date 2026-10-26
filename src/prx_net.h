/* prx_net.h - interposed socket entry points
 *
 * connect/bind/sendto/recvfrom, getsockname/getpeername, accept, socketpair,
 * setsockopt/getsockopt and sendmsg/recvmsg (SCM_CREDENTIALS handling).
 *
 * Part of the shim. Included only by prx.c, in the order listed there:
 * the shim is one translation unit and later parts use what earlier
 * parts define. Do not compile this file on its own.
 */
#ifndef PRX_NET_H
#define PRX_NET_H

EXP int connect(int fd, const void*a, unsigned l) {
    static int(*f)(int, const void*, unsigned);
    u8 na[110];
    unsigned nl = 0;
    int tfd, r = un_fix(a, l, 1, na, &nl, &tfd);
    if (r<0) return-1;
    if (!f) f = nextsym("connect");
    int rv = r ? f(fd, na, nl) : f(fd, a, l);
    int e = geterr();
    un_done(tfd);
    *__errno_location() = e;
    return rv;
}
EXP int bind(int fd, const void*a, unsigned l) {
    static int(*f)(int, const void*, unsigned);
    u8 na[110];
    unsigned nl = 0;
    int tfd, r = un_fix(a, l, 0, na, &nl, &tfd);
    if (r<0) return-1;
    if (!f) f = nextsym("bind");
    int rv = r ? f(fd, na, nl) : f(fd, a, l);
    int e = geterr();
    un_done(tfd);
    *__errno_location() = e;
    return rv;
}
EXP long sendto(int fd, const void*buf, size_t len, int fl, const void*a, unsigned al) {
    static long(*f)(int, const void*, size_t, int, const void*, unsigned);
    u8 na[110];
    unsigned nl = 0;
    int tfd, r = a ? un_fix(a, al, 1, na, &nl, &tfd) : (tfd = -1, 0);
    if (r<0) return-1;
    if (!f) f = nextsym("sendto");
    long rv = r ? f(fd, buf, len, fl, na, nl) : f(fd, buf, len, fl, a, al);
    int e = geterr();
    un_done(tfd);
    *__errno_location() = e;
    return rv;
}
EXP long recvfrom(int fd, void*buf, size_t len, int fl, void*a, unsigned*al) {
    static long(*f)(int, void*, size_t, int, void*, unsigned*);
    if (!f) f = nextsym("recvfrom");
    long r = f ? f(fd, buf, len, fl, a, al) : -1;
    if (r >= 0 && a && al) un_strip(a, al);
    return r;
}
EXP int getsockname(int fd, void*a, unsigned*l) {
    static int(*f)(int, void*, unsigned*);
    if (!f) f = nextsym("getsockname");
    int r = f(fd, a, l);
    if (r == 0) un_strip(a, l);
    return r;
}
EXP int getpeername(int fd, void*a, unsigned*l) {
    static int(*f)(int, void*, unsigned*);
    if (!f) f = nextsym("getpeername");
    int r = f(fd, a, l);
    if (r == 0) un_strip(a, l);
    return r;
}
EXP int accept(int fd, void*a, unsigned*l) {
    static int(*f)(int, void*, unsigned*);
    if (!f) f = nextsym("accept");
    int r = f(fd, a, l);
    if (r >= 0) un_strip(a, l);
    return r;
}
EXP int accept4(int fd, void*a, unsigned*l, int fl) {
    static int(*f)(int, void*, unsigned*, int);
    if (!f) f = nextsym("accept4");
    int r = f(fd, a, l, fl);
    if (r >= 0) un_strip(a, l);
    return r;
}
EXP int socketpair(int domain, int type, int protocol, int sv[2]) {
    static int(*f)(int, int, int, int*);
    if (!f) f = nextsym("socketpair");
    return f ? f(domain, type, protocol, sv) : -1;
}
#define SOL_SOCKET_ 1
#define SO_PASSCRED_ 16
#define SCM_CREDENTIALS_ 2
EXP int setsockopt(int fd, int level, int optname, const void*optval, unsigned int optlen) {
    static int(*real_setsockopt)(int, int, int, const void*, unsigned int);
    if (!real_setsockopt) real_setsockopt = nextsym("setsockopt");
    if (level == SOL_SOCKET_ && optname == SO_PASSCRED_) return 0;
    return real_setsockopt ? real_setsockopt(fd, level, optname, optval, optlen) : 0;
}
EXP int getsockopt(int fd, int level, int optname, void*optval, unsigned int*optlen) {
    static int(*real_getsockopt)(int, int, int, void*, unsigned int*);
    if (!real_getsockopt) real_getsockopt = nextsym("getsockopt");
    if (level == SOL_SOCKET_ && optname == SO_PASSCRED_) {
        if (optval && optlen && *optlen >= sizeof (int)) {
            *(int*) optval = 1;
            *optlen = sizeof (int);
            return 0;
        }
    }
    return real_getsockopt ? real_getsockopt(fd, level, optname, optval, optlen) : 0;
}
struct msghdr_t {
    void*msg_name;
    unsigned int msg_namelen;
    void*msg_iov;
    size_t msg_iovlen;
    void*msg_control;
    size_t msg_controllen;
    int msg_flags;
};
struct cmsghdr_t {
    size_t cmsg_len;
    int cmsg_level;
    int cmsg_type;
};
struct ucred_t {
    u32 pid;
    u32 uid;
    u32 gid;
};
EXP ssize_t sendmsg(int sockfd, const struct msghdr_t*msg, int flags) {
    static ssize_t(*real_sendmsg)(int, const void*, int);
    if (!real_sendmsg) real_sendmsg = nextsym("sendmsg");
    ssize_t ret = real_sendmsg ? real_sendmsg(sockfd, msg, flags) : -1;
    if (ret<0 && *__errno_location() == EPERM) {
        if (msg && msg->msg_iov && msg->msg_iovlen>0) {
            struct iovec_t {
                void*iov_base;
                size_t iov_len;
            }*iov = (struct iovec_t*) msg->msg_iov;
            *__errno_location() = 0;
            return (ssize_t) iov[0].iov_len;
        }
        *__errno_location() = 0;
        return 1;
    }
    return ret;
}
EXP ssize_t recvmsg(int sockfd, struct msghdr_t*msg, int flags) {
    static ssize_t(*real_recvmsg)(int, struct msghdr_t*, int);
    if (!real_recvmsg) real_recvmsg = nextsym("recvmsg");
    ssize_t res = real_recvmsg ? real_recvmsg(sockfd, msg, flags) : -1;
    if (res >= 0 && VROOT() && msg && msg->msg_control && msg->msg_controllen >= sizeof (struct cmsghdr_t)) {
        char*cmsg_ptr = (char*) msg->msg_control;
        size_t clen = msg->msg_controllen;
        while (clen >= sizeof (struct cmsghdr_t)) {
            struct cmsghdr_t*cm = (struct cmsghdr_t*) cmsg_ptr;
            if (cm->cmsg_len<sizeof (struct cmsghdr_t) || cm->cmsg_len>clen) break;
            if (cm->cmsg_level == SOL_SOCKET_ && cm->cmsg_type == SCM_CREDENTIALS_) {
                struct ucred_t*cr = (struct ucred_t*)(cmsg_ptr+sizeof (struct cmsghdr_t));
                if (cm->cmsg_len >= sizeof (struct cmsghdr_t)+sizeof (struct ucred_t)) {
                    cr->uid = 0;
                    cr->gid = 0;
                }
            }
            size_t step = (cm->cmsg_len+sizeof (size_t)-1)&~(sizeof (size_t)-1);
            if (step>clen) break;
            cmsg_ptr += step;
            clen -= step;
        }
    }
    return res;
}

#endif /* PRX_NET_H */
