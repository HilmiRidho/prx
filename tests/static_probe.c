/* Test program for `prx login --ptrace`: build with `cc -static`. Each mode
 * prints something that differs between a path that is translated into the
 * rootfs and one that reaches the host. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static int cat(const char *p) {
    char b[512];
    int fd = open(p, O_RDONLY);
    ssize_t n;
    if (fd < 0) { printf("open %s: %s\n", p, strerror(errno)); return 1; }
    while ((n = read(fd, b, sizeof b)) > 0) fwrite(b, 1, (size_t) n, stdout);
    close(fd);
    return 0;
}
static volatile sig_atomic_t g_usr1;
static void on_usr1(int s) { (void) s; g_usr1++; }
static void *texec_thr(void *a) { char **v = a; syscall(SYS_execve, v[0], v, environ); printf("execve %s: %s\n", v[0], strerror(errno)); return NULL; }
static void *thr(void *a) { cat((const char *) a); return NULL; }

int main(int argc, char **argv) {
    const char *m = argc > 1 ? argv[1] : "";
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!strcmp(m, "cat")) return cat(argv[2]);
    if (!strcmp(m, "cwd")) {
        char b[512];
        if (chdir("/root") < 0) { perror("chdir"); return 1; }
        printf("cwd=%s\n", getcwd(b, sizeof b));
        return cat("../etc/os-release");
    }
    if (!strcmp(m, "fs")) {                       /* create/rename/symlink/readlink/stat/unlink */
        char b[256]; struct stat st; ssize_t n;
        if (mkdir("/tmp/sp", 0755) < 0 && errno != EEXIST) { perror("mkdir"); return 1; }
        int fd = open("/tmp/sp/a", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { perror("create"); return 1; }
        if (write(fd, "hello\n", 6) != 6) return 1;
        close(fd);
        if (rename("/tmp/sp/a", "/tmp/sp/b") < 0) { perror("rename"); return 1; }
        if (symlink("/tmp/sp/b", "/tmp/sp/l") < 0) { perror("symlink"); return 1; }
        n = readlink("/tmp/sp/l", b, sizeof b - 1);
        if (n < 0) { perror("readlink"); return 1; }
        b[n] = 0;
        printf("link=%s\n", b);
        if (stat("/tmp/sp/l", &st) < 0) { perror("stat"); return 1; }
        printf("size=%ld\n", (long) st.st_size);
        if (access("/tmp/sp/nope", F_OK) == 0) return 1;
        printf("nope=%s\n", strerror(errno));
        if (unlink("/tmp/sp/l") < 0 || unlink("/tmp/sp/b") < 0 || rmdir("/tmp/sp") < 0) { perror("cleanup"); return 1; }
        return 0;
    }
    if (!strcmp(m, "list")) {
        DIR *d = opendir(argv[2]); struct dirent *e;
        if (!d) { perror("opendir"); return 1; }
        while ((e = readdir(d))) if (e->d_name[0] != '.') printf("%s\n", e->d_name);
        closedir(d);
        return 0;
    }
    if (!strcmp(m, "fork")) {
        pid_t c = fork();
        if (c == 0) { cat(argv[2]); _exit(7); }
        int st; waitpid(c, &st, 0);
        printf("child exit=%d\n", WEXITSTATUS(st));
        return 0;
    }
    if (!strcmp(m, "threads")) {
        pthread_t t[8]; int i;
        for (i = 0; i < 8; i++) pthread_create(&t[i], NULL, thr, argv[2]);
        for (i = 0; i < 8; i++) pthread_join(t[i], NULL);
        return 0;
    }
    if (!strcmp(m, "exec")) {                     /* raw execve: bypasses the shim */
        syscall(SYS_execve, argv[2], argv + 2, environ);
        printf("execve %s: %s\n", argv[2], strerror(errno));
        return 1;
    }
    if (!strcmp(m, "spawn")) {                    /* fork + raw execve + wait */
        pid_t c = fork();
        if (c == 0) { syscall(SYS_execve, argv[2], argv + 2, environ); printf("execve %s: %s\n", argv[2], strerror(errno)); _exit(99); }
        int st; waitpid(c, &st, 0);
        printf("spawned exit=%d\n", WEXITSTATUS(st));
        return 0;
    }
    if (!strcmp(m, "vspawn")) {                   /* vfork + raw execve, like Go's os/exec */
        pid_t c = vfork();
        if (c == 0) { syscall(SYS_execve, argv[2], argv + 2, environ); _exit(99); }
        int st; waitpid(c, &st, 0);
        printf("vspawned exit=%d\n", WEXITSTATUS(st));
        return 0;
    }
    if (!strcmp(m, "pause")) {                    /* wait for signals, report them, exit after SIGTERM */
        int i, seen = 0;
        signal(SIGUSR1, on_usr1);
        printf("pid=%d\n", (int) getpid());
        for (i = 0; i < 100; i++) {
            usleep(100000);
            if (g_usr1 != seen) { seen = g_usr1; printf("usr1 x%d\n", seen); }
        }
        printf("timeout\n");
        return 3;
    }
    if (!strcmp(m, "texec")) {                    /* execve from a non-leader thread, like Go's syscall.Exec */
        pthread_t t;
        pthread_create(&t, NULL, texec_thr, argv + 2);
        pthread_join(t, NULL);
        return 1;                                 /* not reached unless the execve failed */
    }
    if (!strcmp(m, "exit")) return atoi(argv[2]);
    fprintf(stderr, "usage: static_probe MODE ...\n");
    return 2;
}
