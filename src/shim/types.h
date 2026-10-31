/* types.h - basic integer types, open/at/errno/access constants
 * Part of the prx shim (unity build: included by src/prx.c, not a standalone header). */
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
#define EBADF 9
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
#define ENOSPC 28
#define EOPNOTSUPP 95
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0
