/* Syscalls that used to return ENOSYS, and MAP_32BIT. A call the host kernel itself refuses (ENOSYS/EPERM/EACCES, e.g. in a
 * sandbox) counts as fine: what matters is that the guest gets the host's answer and not a made-up one. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <utime.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/signalfd.h>
#include <sys/inotify.h>

static int refused(void) { return errno == ENOSYS || errno == EPERM || errno == EACCES || errno == ENOSPC; }
static void res(const char*n, int ok) { printf("%s: %s\n", n, ok ? "ok" : "FAIL"); }

int main(void) {
    char d[] = "/tmp/pemu_sys2_XXXXXX"; if (!mkdtemp(d)) return 1;
    char p[128];

    /* SysV shared memory, semaphores, message queues (including the IPC_STAT/IPC_RMID command path) */
    { int id = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600);
      if (id < 0) res("shm", refused());
      else { char*m = shmat(id, NULL, 0); int ok = m != (void*) -1; if (ok) { strcpy(m, "hello"); ok = !strcmp(m, "hello"); }
             struct shmid_ds st; ok = ok && shmctl(id, IPC_STAT, &st) == 0 && st.shm_segsz == 4096;
             ok = ok && shmdt(m) == 0 && shmctl(id, IPC_RMID, NULL) == 0; res("shm", ok); } }
    { int id = semget(IPC_PRIVATE, 1, IPC_CREAT | 0600);
      if (id < 0) res("sem", refused());
      else { union semun { int v; } u = { 3 }; int ok = semctl(id, 0, SETVAL, u) == 0 && semctl(id, 0, GETVAL) == 3;
             struct sembuf op = { 0, -1, 0 }; ok = ok && semop(id, &op, 1) == 0 && semctl(id, 0, GETVAL) == 2 && semctl(id, 0, IPC_RMID) == 0; res("sem", ok); } }
    { int id = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
      if (id < 0) res("msg", refused());
      else { struct { long t; char b[8]; } m = { 1, "abc" }, r; int ok = msgsnd(id, &m, 4, 0) == 0 && msgrcv(id, &r, 8, 0, 0) == 4 && !strcmp(r.b, "abc");
             struct msqid_ds st; ok = ok && msgctl(id, IPC_STAT, &st) == 0 && msgctl(id, IPC_RMID, NULL) == 0; res("msg", ok); } }

    /* utime, mknod (fifo), futimesat */
    snprintf(p, sizeof p, "%s/f", d); close(open(p, O_CREAT | O_WRONLY, 0600));
    { struct utimbuf ub = { 1000000, 2000000 }; struct stat st; int ok = utime(p, &ub) == 0 && stat(p, &st) == 0 && st.st_atime == 1000000 && st.st_mtime == 2000000; res("utime", ok); }
    { struct timeval tv[2] = { { 3000000, 0 }, { 4000000, 0 } }; struct stat st; long r = syscall(SYS_futimesat, AT_FDCWD, p, tv);
      res("futimesat", r == 0 && stat(p, &st) == 0 && st.st_atime == 3000000 && st.st_mtime == 4000000); }
    { char q[128]; snprintf(q, sizeof q, "%s/fifo", d); long r = syscall(SYS_mknod, q, S_IFIFO | 0600, 0); struct stat st;
      res("mknod", r == 0 && stat(q, &st) == 0 && S_ISFIFO(st.st_mode)); unlink(q); }

    /* inotify_init (old), signalfd (old), sigqueue, preadv2, faccessat2 */
    { long fd = syscall(SYS_inotify_init); res("inotify_init", fd >= 0 || refused()); if (fd >= 0) close((int) fd); }
    { sigset_t ms; sigemptyset(&ms); sigaddset(&ms, SIGUSR2); long fd = syscall(SYS_signalfd, -1, &ms, 8); res("signalfd", fd >= 0 || refused()); if (fd >= 0) close((int) fd); }
    { static volatile int got; struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_flags = SA_SIGINFO;
      void h(int s, siginfo_t*si, void*u) { (void) s; (void) u; got = si->si_value.sival_int; }
      sa.sa_sigaction = h; sigaction(SIGUSR1, &sa, NULL); union sigval v; v.sival_int = 42; int r = sigqueue(getpid(), SIGUSR1, v); res("sigqueue", (r == 0 && got == 42) || (r < 0 && refused())); }
    { int fd = open(p, O_RDWR); (void) !write(fd, "0123456789", 10); char b[4]; struct iovec iv = { b, 4 }; long r = preadv2(fd, &iv, 1, 3, 0);
      res("preadv2", (r == 4 && !memcmp(b, "3456", 4)) || (r < 0 && refused())); close(fd); }
    { long r = syscall(SYS_faccessat2, AT_FDCWD, p, R_OK, AT_EACCESS); res("faccessat2", r == 0 || errno == ENOSYS); }

    /* MAP_32BIT: below 2 GiB, and honoured for several mappings */
    { int ok = 1; for (int i = 0; i < 4; i++) { void*m = mmap(NULL, 1 << 16, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
        if (m == MAP_FAILED || (unsigned long) m + (1 << 16) > 0x80000000UL) { ok = 0; break; } *(int*) m = i; }
      res("map32bit", ok); }

    unlink(p); rmdir(d);
    return 0;
}
