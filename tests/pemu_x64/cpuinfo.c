/* /proc/cpuinfo must agree with cpuid about the features programs look for, and be a normal seekable file. Natively both are
 * the real CPU's; under pemu cpuinfo is synthesized, so this catches the two drifting apart. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <cpuid.h>
#include <sys/syscall.h>
#include <linux/openat2.h>

static int has_flag(const char*txt, const char*w) {
    const char*f = strstr(txt, "\nflags"); if (!f) f = strstr(txt, "flags"); if (!f) return 0;
    const char*e = strchr(f + 1, '\n'); size_t wl = strlen(w);
    for (const char*p = strchr(f, ':'); p && p < e; p++) if ((p[0] == ' ' || p[0] == '\t' || p[0] == ':') && !strncmp(p + 1, w, wl) && (p[1 + wl] == ' ' || p[1 + wl] == '\n')) return 1;
    return 0;
}
static char*slurp(int fd) { size_t cap = 1 << 16, n = 0; char*b = malloc(cap); ssize_t r; while ((r = read(fd, b + n, cap - n - 1)) > 0) { n += (size_t) r; if (n + 1 >= cap) b = realloc(b, cap *= 2); } b[n] = 0; return b; }
static int consistent(const char*t) {
    unsigned a, b, c, d; __cpuid(1, a, b, c, d); int ok = 1;
    struct { const char*n; int v; } f[] = { {"sse2", (d >> 26) & 1}, {"pni", c & 1}, {"ssse3", (c >> 9) & 1}, {"sse4_1", (c >> 19) & 1}, {"sse4_2", (c >> 20) & 1}, {"popcnt", (c >> 23) & 1}, {"cx16", (c >> 13) & 1} };
    for (unsigned i = 0; i < sizeof f / sizeof f[0]; i++) if (has_flag(t, f[i].n) != f[i].v) { printf("MISMATCH %s: cpuid %d cpuinfo %d\n", f[i].n, f[i].v, has_flag(t, f[i].n)); ok = 0; }
    return ok;
}
int main(void) {
    int fd = open("/proc/cpuinfo", O_RDONLY); if (fd < 0) { puts("open failed"); return 1; }
    char*t = slurp(fd);
    printf("flags consistent with cpuid: %s\n", consistent(t) ? "yes" : "NO");
    int np = 0; for (const char*p = t; (p = strstr(p, "processor")); p++) np++;
    printf("processors listed: %s\n", np >= 1 ? ">=1" : "none");
    lseek(fd, 0, SEEK_SET); char*t2 = slurp(fd); printf("seek and reread: %s\n", !strcmp(t, t2) ? "same" : "DIFFERENT");
    close(fd); free(t); free(t2);
    FILE*fp = fopen("/proc/cpuinfo", "r"); char line[4096]; int fl = 0; while (fp && fgets(line, sizeof line, fp)) if (!strncmp(line, "flags", 5)) fl = 1; if (fp) fclose(fp);
    printf("fopen + fgets: %s\n", fl ? "flags line found" : "NO flags line");
    /* openat2: the flags travel inside a struct; a directory open and the cpuinfo open must both work (or the host lacks openat2) */
    struct open_how how; memset(&how, 0, sizeof how); how.flags = O_RDONLY | O_DIRECTORY;
    long r = syscall(SYS_openat2, AT_FDCWD, "/tmp", &how, sizeof how);
    printf("openat2 directory: %s\n", r >= 0 || errno == ENOSYS || errno == EPERM ? "ok" : "FAIL"); if (r >= 0) close((int) r);
    memset(&how, 0, sizeof how); how.flags = O_RDONLY; r = syscall(SYS_openat2, AT_FDCWD, "/proc/cpuinfo", &how, sizeof how);
    if (r >= 0) { char*u = slurp((int) r); printf("openat2 cpuinfo consistent: %s\n", consistent(u) ? "yes" : "NO"); free(u); close((int) r); } else printf("openat2 cpuinfo consistent: %s\n", errno == ENOSYS || errno == EPERM ? "yes" : "NO");
    r = syscall(SYS_openat2, AT_FDCWD, "/tmp", &how, 8); printf("openat2 short struct: %s\n", r < 0 && (errno == EINVAL || errno == ENOSYS || errno == EPERM) ? "rejected" : "ACCEPTED");
    return 0;
}
