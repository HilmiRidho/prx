/* No libc at all, like a Go binary: opens and prints argv[1] with raw syscalls. */
static long sys(long n, long a, long b, long c, long d) {
    long r;
    register long r10 __asm__("r10") = d;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10) : "rcx", "r11", "memory");
    return r;
}
void _start_c(long *sp) {
    const char *path = (const char *) sp[2];
    char buf[256];
    long fd = sys(257, -100, (long) path, 0, 0), n;
    if (fd < 0) sys(231, 1, 0, 0, 0);
    while ((n = sys(0, fd, (long) buf, sizeof buf, 0)) > 0) sys(1, 1, (long) buf, n, 0);
    sys(231, 0, 0, 0, 0);
}
__asm__(".globl _start\n_start:\n mov %rsp, %rdi\n and $-16, %rsp\n call _start_c\n");
