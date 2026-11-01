/* Run with the shim preloaded: shim_fs ROOT
 * ROOT is the fake rootfs given as PRX_ROOTFS; it must hold etc/services (prxsvc 4242/tcp
 * prxalias), etc/protocols (prxproto 250 PRXP), etc/hosts (10.9.8.7 prxhost), etc/mtab
 * (prxdev ...), etc/prxmarker, dev/shm/ and tmp/. Each check fails when glibc reads the
 * HOST file instead of the guest one. Exit status is a bitmask of failed checks. */
#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <mntent.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

static int raw_exists(const char *p) { return syscall(SYS_access, p, 0) == 0; }

int main(int argc, char **argv) {
    int bad = 0;
    char b[512];
    if (argc < 2) return 255;
    struct servent *s = getservbyname("prxalias", "tcp");
    if (!s || ntohs(s->s_port) != 4242 || strcmp(s->s_name, "prxsvc")) bad |= 1;
    s = getservbyport(htons(4242), "tcp");
    if (!s || strcmp(s->s_name, "prxsvc")) bad |= 2;
    struct protoent *p = getprotobyname("prxproto");
    if (!p || p->p_proto != 250) bad |= 4;
    p = getprotobynumber(250);
    if (!p || strcmp(p->p_name, "prxproto")) bad |= 8;
    struct hostent h, *r = NULL;
    char hb[1024];
    int he;
    if (gethostbyname_r("prxhost", &h, hb, sizeof hb, &r, &he) != 0 || !r) bad |= 16;
    int fd = shm_open("/prxshm", O_CREAT | O_RDWR, 0600);
    snprintf(b, sizeof b, "%s/dev/shm/prxshm", argv[1]);
    if (fd < 0 || !raw_exists(b)) bad |= 32;
    if (fd >= 0) close(fd);
    shm_unlink("/prxshm");
    if (raw_exists(b)) bad |= 32;
    char tn[] = "/tmp/prxXXXXXX.txt";
    fd = mkostemps(tn, 4, 0);
    snprintf(b, sizeof b, "%s%s", argv[1], tn);
    if (fd < 0 || !raw_exists(b)) bad |= 64;
    if (fd >= 0) close(fd);
    unlink(tn);
    if (ftok("/etc/prxmarker", 1) == (key_t)-1) bad |= 128;
    FILE *m = setmntent("/etc/mtab", "r");
    int found = 0;
    if (m) {
        struct mntent *e;
        while ((e = getmntent(m))) if (!strcmp(e->mnt_fsname, "prxdev")) found = 1;
        endmntent(m);
    }
    if (!found) bad |= 256;
    struct dirent **nl;
    int n = scandirat(AT_FDCWD, "/etc", &nl, 0, 0);
    found = 0;
    for (int i = 0; i < n; i++) if (!strcmp(nl[i]->d_name, "prxmarker")) found = 1;
    if (!found) bad |= 512;
    return bad & 255 ? (bad & 255) : (bad ? 1 : 0);
}
