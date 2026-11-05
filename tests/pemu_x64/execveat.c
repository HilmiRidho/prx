/* execveat: by file descriptor (AT_EMPTY_PATH), relative to a directory fd, with AT_FDCWD, and the error cases. The program runs
 * itself again as the exec'd image. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/syscall.h>

extern char**environ;
static int ex(int dfd, const char*path, const char*tag, int flags) {
    fflush(stdout);
    pid_t p = fork();
    if (p == 0) {
        char*args[] = { (char*) "execveat-test", (char*) tag, NULL };
        syscall(SYS_execveat, dfd, path, args, environ, flags);
        printf("execveat failed: %d\n", errno); fflush(stdout); _exit(99);
    }
    int st; waitpid(p, &st, 0);
    printf("status %d\n", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    return 0;
}

int main(int argc, char**argv) {
    if (argc > 1) { printf("child %s\n", argv[1]); return 0; }
    char dir[512]; strncpy(dir, argv[0], sizeof dir - 1); dir[sizeof dir - 1] = 0;
    char*slash = strrchr(dir, '/'); if (!slash) return 1;
    const char*base = slash + 1; *slash = 0;
    int self = open(argv[0], O_RDONLY), dfd = open(dir, O_RDONLY | O_DIRECTORY);
    ex(self, "", "empty", 0x1000 /*AT_EMPTY_PATH*/);
    ex(dfd, base, "dirfd", 0);
    ex(AT_FDCWD, argv[0], "cwd", 0);
    errno = 0; long r = syscall(SYS_execveat, AT_FDCWD, "/nonexistent-pemu", argv, environ, 0);
    printf("enoent %s\n", r == -1 && errno == ENOENT ? "ok" : "FAIL");
    errno = 0; r = syscall(SYS_execveat, AT_FDCWD, argv[0], argv, environ, 0x4000);
    printf("einval %s\n", r == -1 && errno == EINVAL ? "ok" : "FAIL");
    errno = 0; r = syscall(SYS_execveat, self, "", argv, environ, 0);
    printf("empty without flag %s\n", r == -1 && errno == ENOENT ? "ok" : "FAIL");
    return 0;
}
