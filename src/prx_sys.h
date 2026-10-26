/* prx_sys.h - freestanding runtime for the shim
 *
 * Types, errno and open(2) constants, per-architecture stat and syscall
 * layouts (x86_64, aarch64, i386), raw syscall wrappers, 64-bit division
 * helpers (the shim has no libc), pread_ and the dlsym(RTLD_NEXT) lookup.
 *
 * Part of the shim. Included only by prx.c, in the order listed there:
 * the shim is one translation unit and later parts use what earlier
 * parts define. Do not compile this file on its own.
 */
#ifndef PRX_SYS_H
#define PRX_SYS_H

typedef unsigned long size_t;
typedef long ssize_t;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef unsigned long long u64;
typedef long long s64;
#define NULL ((void*)0)
#define EXP __attribute__((visibility("default")))
#define PM 4096
#define AT_FDCWD (-100)
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0x40
#define O_EXCL 0x80
#define O_TRUNC 0x200
#define O_APPEND 0x400
#define O_CLOEXEC 0x80000
#define O_ACCMODE_ 3
#if defined(__i386__)
#define O_LARGEFILE_ 0x8000
#else
#define O_LARGEFILE_ 0
#endif
#if defined(__aarch64__)
#define O_NOFOLLOW 0x8000
#else
#define O_NOFOLLOW 0x20000
#endif
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_SYMLINK_FOLLOW 0x400
#define AT_REMOVEDIR 0x200
#define EPERM 1
#define ENOENT 2
#define ENOEXEC 8
#define EACCES 13
#define EFAULT 14
#define EEXIST 17
#define ENOTDIR 20
#define EINVAL 22
#define ERANGE 34
#define ENAMETOOLONG 36
#define ENOSYS 38
#define ELOOP 40
#define ENOMEM 12
#define EINTR 4
#define ESTALE 116
#define E2BIG 7
#define EOPNOTSUPP 95
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0
#if defined(__aarch64__)
#define N_fchdir 50
#define N_getcwd 17
#define N_pipe2 59
#define N_mknodat 33
#define N_mkdirat 34
#define N_unlinkat 35
#define N_symlinkat 36
#define N_linkat 37
#define N_renameat 38
#define N_truncate 45
#define N_faccessat 48
#define N_chdir 49
#define N_fchmodat 53
#define N_fchownat 54
#define N_openat 56
#define N_close 57
#define N_lseek 62
#define N_read 63
#define N_write 64
#define N_fchmod 52
#define MODE_OFF 16
#define N_pread64 67
#define N_readlinkat 78
#define N_newfstatat 79
#define N_fstat 80
#define N_utimensat 88
#define N_capget 90
#define N_capset 91
#define N_getpid 172
#define N_exit_group 94
#define N_sched_getaffinity 123
#define N_getppid 173
#define N_getuid 174
#define N_geteuid 175
#define N_getgid 176
#define N_getegid 177
#define N_gettid 178
#define N_execve 221
#define N_renameat2 276
#define N_getrandom 278
#define N_memfd_create 279
#define N_statx 291
#define N_execveat 281
#define N_setxattr 5
#define N_lsetxattr 6
#define N_getxattr 8
#define N_lgetxattr 9
#define N_listxattr 11
#define N_llistxattr 12
#define N_removexattr 14
#define N_lremovexattr 15
#define N_statfs 43
#define N_fstatfs 44
#define N_ioctl 29
#define N_fcntl 25
#define OFF_UID 24
#define OFF_NLINK 20
#define OFF_INO 8
#elif defined(__x86_64__)
#define N_fchdir 81
#define N_read 0
#define N_write 1
#define N_close 3
#define N_fstat 5
#define N_lseek 8
#define N_ioctl 16
#define N_pread64 17
#define N_getpid 39
#define N_exit_group 231
#define N_sched_getaffinity 204
#define N_execve 59
#define N_fcntl 72
#define N_truncate 76
#define N_getcwd 79
#define N_chdir 80
#define N_fchmod 91
#define MODE_OFF 24
#define N_getuid 102
#define N_getgid 104
#define N_geteuid 107
#define N_getegid 108
#define N_getppid 110
#define N_capget 125
#define N_capset 126
#define N_statfs 137
#define N_fstatfs 138
#define N_gettid 186
#define N_setxattr 188
#define N_lsetxattr 189
#define N_getxattr 191
#define N_lgetxattr 192
#define N_listxattr 194
#define N_llistxattr 195
#define N_removexattr 197
#define N_lremovexattr 198
#define N_openat 257
#define N_mkdirat 258
#define N_mknodat 259
#define N_fchownat 260
#define N_newfstatat 262
#define N_unlinkat 263
#define N_renameat 264
#define N_linkat 265
#define N_symlinkat 266
#define N_readlinkat 267
#define N_fchmodat 268
#define N_faccessat 269
#define N_utimensat 280
#define N_pipe2 293
#define N_renameat2 316
#define N_getrandom 318
#define N_memfd_create 319
#define N_execveat 322
#define N_statx 332
#define OFF_UID 28
#define OFF_NLINK 16
#define OFF_INO 8
#elif defined(__i386__)
/* i386 guest (qemu-user only). Kernel i386 ABI: 32-bit longs, *64 stat family,
   32-bit uid/gid syscalls, 64-bit file offsets passed as lo/hi register pairs. */
#define N_read 3
#define N_write 4
#define N_close 6
#define N_execve 11
#define N_chdir 12
#define N_lseek 19
#define N_getpid 20
#define N_exit_group 252
#define N_getppid 64
#define N_ioctl 54
#define N_fcntl 55
#define N_truncate 92
#define N_fchmod 94
#define N_statfs 99
#define N_fstatfs 100
#define N_fchdir 133
#define N_pread64 180
#define N_capget 184
#define N_capset 185
#define N_getcwd 183
#define N_truncate64 193
#define N_fstat 197
#define N_getuid 199
#define N_getgid 200
#define N_geteuid 201
#define N_getegid 202
#define N_gettid 224
#define N_setxattr 226
#define N_lsetxattr 227
#define N_getxattr 229
#define N_lgetxattr 230
#define N_listxattr 232
#define N_llistxattr 233
#define N_removexattr 235
#define N_lremovexattr 236
#define N_sched_getaffinity 242
#define N_statfs64 268
#define N_fstatfs64 269
#define N_openat 295
#define N_mkdirat 296
#define N_mknodat 297
#define N_fchownat 298
#define N_newfstatat 300
#define N_unlinkat 301
#define N_renameat 302
#define N_linkat 303
#define N_symlinkat 304
#define N_readlinkat 305
#define N_fchmodat 306
#define N_faccessat 307
#define N_utimensat 320
#define N_pipe2 331
#define N_renameat2 353
#define N_getrandom 355
#define N_memfd_create 356
#define N_execveat 358
#define N_statx 383
#define N_utimensat_time64 412
#define MODE_OFF 16
#define OFF_UID 24
#define OFF_NLINK 20
#define OFF_INO 88
#else
#error "Unsupported target architecture."
#endif
#define N_openat2 437
#define N_faccessat2 439
#if defined(__aarch64__)
static inline long sc(long n, long a, long b, long c, long d, long e, long f) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;
    __asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory", "cc");
    return x0;
}
#elif defined(__i386__)
/* int 0x80 with up to six arguments. ebx (PIC register) and ebp are saved by
   hand; the arguments are loaded from a stack array through eax. */
static inline long sc(long n, long a, long b, long c, long d, long e, long f) {
    long v[7] = {n, a, b, c, d, e, f};
    long ret;
    __asm__ volatile(
        "push %%ebx\n\t"
        "push %%ebp\n\t"
        "mov 4(%%eax), %%ebx\n\t"
        "mov 8(%%eax), %%ecx\n\t"
        "mov 12(%%eax), %%edx\n\t"
        "mov 16(%%eax), %%esi\n\t"
        "mov 20(%%eax), %%edi\n\t"
        "mov 24(%%eax), %%ebp\n\t"
        "mov (%%eax), %%eax\n\t"
        "int $0x80\n\t"
        "pop %%ebp\n\t"
        "pop %%ebx"
        : "=a"(ret) : "a"(v) : "ecx", "edx", "esi", "edi", "memory", "cc");
    return ret;
}
#else
static inline long sc(long n, long a, long b, long c, long d, long e, long f) {
    long ret;
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return ret;
}
#endif
#if defined(__i386__)
/* The shim is linked with -nostdlib, so the 64-bit division/modulo helpers the
   compiler emits on 32-bit x86 are provided here (shift/subtract, only used for
   a few rare formatting paths). */
static u64 udivmod64(u64 n, u64 d, u64*rem) {
    u64 q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r<<1)|((n>>i)&1);
        if (r >= d) {
            r -= d;
            q |= (u64)1<<i;
        }
    }
    if (rem)*rem = r;
    return q;
}
__attribute__((visibility("hidden"))) u64 __udivdi3(u64 a, u64 b) {
    return udivmod64(a, b, 0);
}
__attribute__((visibility("hidden"))) u64 __umoddi3(u64 a, u64 b) {
    u64 r;
    udivmod64(a, b, &r);
    return r;
}
#endif
#define SC3(n,a,b,c) sc((n),(long)(a),(long)(b),(long)(c),0,0,0)
#define SC4(n,a,b,c,d) sc((n),(long)(a),(long)(b),(long)(c),(long)(d),0,0)
#define SC5(n,a,b,c,d,e) sc((n),(long)(a),(long)(b),(long)(c),(long)(d),(long)(e),0)
static long pread_(long fd, void*b, long n, u64 off) {
#if defined(__i386__)
    return sc(N_pread64, fd, (long) b, n, (long)(u32) off, (long)(u32)(off>>32), 0);
#else
    return sc(N_pread64, fd, (long) b, n, (long) off, 0, 0);
#endif
}
extern int*__errno_location(void);
extern void*malloc(size_t);
extern void*realloc(void*, size_t);
extern void free(void*);
extern void*memcpy(void*, const void*, size_t);
extern void*memmove(void*, const void*, size_t);
extern void*memset(void*, int, size_t);
extern int memcmp(const void*, const void*, size_t);
extern size_t strlen(const char*);
extern int strncmp(const char*, const char*, size_t);
extern int strcmp(const char*, const char*);
extern char*getenv(const char*);
extern void*dlsym(void*, const char*);
extern char**environ;
extern int waitpid(int, int*, int);
extern int pipe2(int*, int);
extern void*fdopen(int, const char*);
extern int fclose(void*);
extern int fileno(void*);
extern void*signal(int, void*);
extern int socket(int, int, int);
extern int connect(int, const void*, unsigned);
extern int send(int, const void*, size_t, int);
extern long recv(int, void*, size_t, int);
extern int setsockopt(int, int, int, const void*, unsigned);
extern int posix_spawn_file_actions_init(void*);
extern int posix_spawn_file_actions_adddup2(void*, int, int);
extern int posix_spawn_file_actions_destroy(void*);
extern int posix_spawnattr_init(void*);
extern int posix_spawnattr_destroy(void*);
extern int posix_spawnattr_setsigdefault(void*, const void*);
extern int posix_spawnattr_setflags(void*, short);
extern int sigemptyset(void*);
extern int sigaddset(void*, int);
#define RTLD_NEXT ((void*)-1L)
static void*nextsym(const char*n) {
    return dlsym(RTLD_NEXT, n);
}
static long ret(long r) {
    if (r<0 && r>-4096) {
        *__errno_location() = (int)-r;
        return-1;
    }
    return r;
}
static long fail(int e) {
    *__errno_location() = e;
    return-1;
}
static int geterr(void) {
    return*__errno_location();
}

#endif /* PRX_SYS_H */
