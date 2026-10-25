/* Freestanding 32-bit test of the i386 shim's raw syscall layer. It includes
 * src/prx.c directly and runs without libc (own memcpy & co. below), so it
 * needs only `gcc -m32` and a kernel that executes 32-bit binaries; it does
 * not need qemu. Prints "RESULT PASS" and exits 0 on success. */
unsigned long strlen_(const char*s);
#include "../src/prx.c"
void*memcpy(void*d, const void*s, size_t n) { char*a = d; const char*b = s; while (n--) *a++ = *b++; return d; }
void*memmove(void*d, const void*s, size_t n) { char*a = d; const char*b = s; if (a < b) { while (n--) *a++ = *b++; } else { a += n; b += n; while (n--) *--a = *--b; } return d; }
void*memset(void*d, int c, size_t n) { char*a = d; while (n--) *a++ = (char) c; return d; }
int memcmp(const void*x, const void*y, size_t n) { const unsigned char*a = x, *b = y; while (n--) { if (*a != *b) return *a - *b; a++; b++; } return 0; }
size_t strlen(const char*s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char*a, const char*b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
int strncmp(const char*a, const char*b, size_t n) { while (n && *a && *a == *b) { a++; b++; n--; } return n ? (unsigned char)*a - (unsigned char)*b : 0; }
unsigned long strlen_(const char*s) { return strlen(s); }
int*__errno_location(void) { static int e; return &e; }
__asm__(".globl _start\n_start:\n\txor %ebp,%ebp\n\tcall _start_c\n");

static int bad;
static void out(const char*s) { sc(N_write, 1, (long) s, (long) strlen(s), 0, 0, 0); }
#define CHK(name, cond) do { if (!(cond)) { bad++; out("FAIL " name "\n"); } } while (0)

void _start_c(void) {
    u8 a[96], d[108], d2[108], c32[88];
    g_inited = 1;
    CHK("stat64 syscall", sc(N_newfstatat, AT_FDCWD, (long) "/etc/passwd", (long) a, 0, 0, 0) == 0);
    /* legacy struct stat conversion */
    CHK("stat_conv", stat_conv(a, c32) == 0 && *(u32*)(c32+16) == *(u32*)(a+16) && *(u32*)(c32+44) == (u32) *(u64*)(a+44));
    /* _TIME_BITS=64 entry points agree with stat64 */
    CHK("time64 stat", __stat64_time64("/etc/passwd", d) == 0);
    CHK("time64 mode/nlink/uid/gid", !memcmp(d+16, a+16, 16));
    CHK("time64 ino/dev/size", *(u64*)(d+8) == *(u64*)(a+88) && *(u64*) d == *(u64*) a && *(u64*)(d+40) == *(u64*)(a+44));
    CHK("time64 mtime", *(u32*)(d+76) == *(u32*)(a+72) && *(u32*)(d+80) == 0 && *(u32*)(d+84) == *(u32*)(a+76));
    long fd = sc(N_openat, AT_FDCWD, (long) "/etc/passwd", O_RDONLY|O_LARGEFILE_, 0, 0, 0);
    CHK("time64 fstat/fstatat agree", __fstat64_time64((int) fd, d2) == 0 && !memcmp(d, d2, 108) && __fstatat64_time64(AT_FDCWD, "/etc/passwd", d2, 0) == 0 && !memcmp(d, d2, 108));
    CHK("time64 lstat dir", __lstat64_time64("/etc", d2) == 0 && (*(u32*)(d2+16)&0170000) == 0040000);
    CHK("time64 rdev", __stat64_time64("/dev/null", d2) == 0 && *(u64*)(d2+32) == 0x103);
    CHK("time64 errno", __stat64_time64("/nonexistent/zzz", d2) == -1 && *__errno_location() == 2);
    /* pread64 with lo/hi offset pair */
    char buf[8];
    CHK("pread64", pread_(fd, buf, 4, 2) == 4 && buf[0] == 'o' && buf[1] == 't');
    /* 64-bit division helpers (no libgcc) */
    u64 big = 1000000000000ULL;
    CHK("udiv/umod", big/7ULL == 142857142857ULL && big%7ULL == 1);
    /* a 2040 timestamp survives (impossible through stat64) */
    long t = sc(N_openat, AT_FDCWD, (long) "/tmp/prx_t64_probe", O_WRONLY|O_CREAT|O_TRUNC|O_LARGEFILE_, 0600, 0, 0);
    long long ts[4] = {3208988800LL, 123456789, 3208988800LL, 987654321};
    CHK("year 2040 round trip", sc(N_utimensat_time64, AT_FDCWD, (long) "/tmp/prx_t64_probe", (long) ts, 0, 0, 0) == 0 && __stat64_time64("/tmp/prx_t64_probe", d2) == 0 && *(long long*)(d2+76) == ts[2] && *(u32*)(d2+84) == 987654321 && *(long long*)(d2+60) == ts[0]);
    sc(N_close, t, 0, 0, 0, 0, 0);
    SC3(N_unlinkat, AT_FDCWD, "/tmp/prx_t64_probe", 0);
    out(bad ? "RESULT FAIL\n" : "RESULT PASS\n");
    sc(1, bad ? 1 : 0, 0, 0, 0, 0, 0);
}
