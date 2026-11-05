/* Syscalls that used to return ENOSYS: getdents (old directory format), setfsuid, and the ones that must never reach the host
 * (ptrace, reboot, iopl, ioperm, init_module), which are refused with EPERM. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>

struct old_dirent { unsigned long d_ino, d_off; unsigned short d_reclen; char d_name[]; };
struct ent { char*name; int type; };
static int cmp(const void*a, const void*b) { return strcmp(((const struct ent*) a)->name, ((const struct ent*) b)->name); }
static void res(const char*n, int ok) { printf("%s: %s\n", n, ok ? "ok" : "FAIL"); }

int main(void) {
    char d[] = "/tmp/pemu_sys3_XXXXXX", p[128];
    if (!mkdtemp(d)) return 1;
    const char*f[] = { "a", "bb", "ccc" };
    for (int i = 0; i < 3; i++) { snprintf(p, sizeof p, "%s/%s", d, f[i]); close(open(p, O_CREAT | O_WRONLY, 0600)); }
    snprintf(p, sizeof p, "%s/dd", d); mkdir(p, 0700);

    int fd = open(d, O_RDONLY | O_DIRECTORY);
    char buf[2048]; struct ent es[16]; int n = 0, bad = 0;
    long r;
    while ((r = syscall(SYS_getdents, fd, buf, sizeof buf)) > 0) {
        for (long off = 0; off < r; ) {
            struct old_dirent*e = (struct old_dirent*)(buf + off);
            int t = (unsigned char) buf[off + e->d_reclen - 1];
            if (e->d_reclen % 8 || e->d_reclen < 24) bad = 1;
            if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..") && n < 16) { es[n].name = strdup(e->d_name); es[n++].type = t; }
            off += e->d_reclen;
        }
    }
    close(fd);
    qsort(es, (size_t) n, sizeof *es, cmp);
    for (int i = 0; i < n; i++) {
        snprintf(p, sizeof p, "%s/%s", d, es[i].name);
        struct stat st; stat(p, &st);
        int dir = S_ISDIR(st.st_mode);
        /* d_type is DT_DIR (4) / DT_REG (8), or DT_UNKNOWN (0) on file systems that do not fill it in */
        int ok = es[i].type == 0 || es[i].type == (dir ? 4 : 8);
        printf("%s %s\n", es[i].name, ok ? (dir ? "dir" : "reg") : "BADTYPE");
    }
    printf("records %s\n", bad || n != 4 ? "BAD" : "ok");

    for (int i = 0; i < 3; i++) { snprintf(p, sizeof p, "%s/%s", d, f[i]); unlink(p); }
    snprintf(p, sizeof p, "%s/dd", d); rmdir(p); rmdir(d);

    errno = 0; r = syscall(SYS_ptrace, 0 /*PTRACE_TRACEME*/, 0, 0, 0);       res("ptrace EPERM", r == -1 && errno == EPERM);
    errno = 0; r = syscall(SYS_reboot, 0, 0, 0, 0);                          res("reboot EPERM", r == -1 && errno == EPERM);
    errno = 0; r = syscall(SYS_iopl, 3);                                     res("iopl EPERM", r == -1 && errno == EPERM);
    errno = 0; r = syscall(SYS_ioperm, 0x80, 1, 1);                          res("ioperm EPERM", r == -1 && errno == EPERM);
    errno = 0; r = syscall(SYS_init_module, 0, 0, "");                       res("init_module EPERM", r == -1 && errno == EPERM);
    errno = 0; r = syscall(SYS_setfsuid, -1);                                res("setfsuid", r == (long) geteuid() || (r == -1 && errno == ENOSYS));
    return 0;
}
