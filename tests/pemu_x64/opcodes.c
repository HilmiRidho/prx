/* Less common integer opcodes (loop*, xlatb, enter, movbe, rd/wr{fs,gs}base, xsave/xrstor) and the way undefined or privileged
 * instructions reach the guest: #UD -> SIGILL, #GP -> SIGSEGV, both with a usable siginfo/ucontext, so a handler can recover
 * (siglongjmp) or skip the instruction (rip += len). Natively the output is the same on any CPU that has these instructions. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <ucontext.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <asm/prctl.h>

static unsigned char area[4096] __attribute__((aligned(64)));
static sigjmp_buf jb;
static volatile int sig, code; static volatile uintptr_t saddr, srip; static volatile int skip;
static void h(int s, siginfo_t*si, void*u) {
    ucontext_t*uc = u;
    sig = s; code = si->si_code; saddr = (uintptr_t) si->si_addr; srip = (uintptr_t) uc->uc_mcontext.gregs[REG_RIP];
    if (skip) { uc->uc_mcontext.gregs[REG_RIP] += skip; return; }
    siglongjmp(jb, 1);
}
#define TRY(what, ...) do { sig = 0; if (!sigsetjmp(jb, 1)) { __VA_ARGS__; } printf("%-9s sig=%d code=%d\n", what, sig, code); } while (0)

/* enter $24,$3 on a fake frame; out[]: rsp before/after, rbp after, [rbp], two copied frame words, frame temp */
void enter_test(unsigned long*out, unsigned long fake_rbp);
asm(".text\n.globl enter_test\nenter_test:\n"
    "  push %rbp\n  push %rbx\n  mov %rdi,%rbx\n  mov %rsp,%r9\n  mov %rsi,%rbp\n  mov %rsp,0(%rbx)\n"
    "  enter $24,$3\n"
    "  mov %rsp,8(%rbx)\n  mov %rbp,16(%rbx)\n  mov 0(%rbp),%rax\n  mov %rax,24(%rbx)\n"
    "  mov -8(%rbp),%rax\n  mov %rax,32(%rbx)\n  mov -16(%rbp),%rax\n  mov %rax,40(%rbx)\n  mov -24(%rbp),%rax\n  mov %rax,48(%rbx)\n"
    "  mov %r9,%rsp\n  pop %rbx\n  pop %rbp\n  ret\n");

__attribute__((noinline)) static void ud2_skip(void) { asm volatile("ud2"); }

static void sec_loop(void) {
    /* loop / loope / loopne: rcx counts down, flags stay as they were */
    { long n = 5, r = 0; asm volatile("1: inc %0; loop 1b" : "+r"(r), "+c"(n)); printf("loop: r=%ld rcx=%ld\n", r, n); }
    { long n = 4, r = 0; asm volatile("1: inc %0; cmp $2,%0; loopne 1b" : "+r"(r), "+c"(n) :: "cc"); printf("loopne: r=%ld rcx=%ld\n", r, n); }
    { long n = 4, r = 0; asm volatile("1: inc %0; cmp %0,%0; loope 1b" : "+r"(r), "+c"(n) :: "cc"); printf("loope(zf): r=%ld rcx=%ld\n", r, n); }
    { long n = 4, r = 0; asm volatile("1: inc %0; cmp $3,%0; loope 1b" : "+r"(r), "+c"(n) :: "cc"); printf("loope(nz): r=%ld rcx=%ld\n", r, n); }
    { long n = 2, cf; asm volatile("stc; 1: loop 1b; sbb %1,%1" : "+c"(n), "=r"(cf) :: "cc"); printf("loop keeps flags: cf=%ld\n", -cf); }
    { long n = 0x100000003L, r = 0; asm volatile("1: inc %0; .byte 0x67; loop 1b" : "+r"(r), "+c"(n)); printf("loop(addr32): r=%ld rcx=%#lx\n", r, n); }
}

static void sec_xlat(void) {
    /* xlatb */
    { unsigned char tb[256], al = 3; for (int i = 0; i < 256; i++) tb[i] = (unsigned char)(255 - i);
      asm volatile("xlatb" : "+a"(al) : "b"(tb) : "memory"); printf("xlatb: %u\n", al); }
}

static void sec_enter(void) {
    /* enter with nesting level 3 */
    { unsigned long fake[8] = {10, 11, 12, 13, 14, 15, 16, 17}, out[8], fr = (unsigned long) &fake[4];
      enter_test(out, fr);
      printf("enter: rsp-%lu rbp=rsp0-%lu old=%d c1=%d c2=%d temp=%d\n", out[0] - out[1], out[0] - out[2], out[3] == fr,
             out[4] == fake[3], out[5] == fake[2], out[6] == out[2]); }
}

static void sec_movbe(void) {
    /* movbe */
    { uint32_t v = 0x11223344, o, st; uint64_t q = 0x1122334455667788ULL, qo, qs; uint16_t w = 0x1122, wo, ws;
      asm volatile("movbe %1,%0" : "=r"(o) : "m"(v)); asm volatile("movbe %1,%0" : "=m"(st) : "r"(v));
      asm volatile("movbe %1,%0" : "=r"(qo) : "m"(q)); asm volatile("movbe %1,%0" : "=m"(qs) : "r"(q));
      asm volatile("movbe %1,%0" : "=r"(wo) : "m"(w)); asm volatile("movbe %1,%0" : "=m"(ws) : "r"(w));
      printf("movbe: %08x %08x %016llx %016llx %04x %04x\n", o, st, (unsigned long long) qo, (unsigned long long) qs, wo, ws); }
}

static void sec_fsgs(void) {
    /* rdfsbase / wrfsbase / rdgsbase / wrgsbase (SIGILL, then no output line, if the kernel does not allow them).
     * Every write is undone inside the same asm statement: nothing may touch the thread pointer in between. */
    { struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = h; sa.sa_flags = SA_SIGINFO; sigaction(SIGILL, &sa, 0);
      unsigned long ref = 0, old = 0, back = 0, gs0 = 0, gs1 = 0, nv; syscall(SYS_arch_prctl, ARCH_GET_FS, &ref);
      nv = ref + 0x1000; skip = 0; sig = 0;
      if (!sigsetjmp(jb, 1)) {
          asm volatile("rdfsbase %[o]; wrfsbase %[n]; rdfsbase %[r]; wrfsbase %[o]" : [o] "=&r"(old), [r] "=&r"(back) : [n] "r"(nv) : "memory");
          asm volatile("rdgsbase %[o]; wrgsbase %[n]; rdgsbase %[r]; wrgsbase %[o]" : [o] "=&r"(gs0), [r] "=&r"(gs1) : [n] "r"(0x7000000000UL) : "memory");
          printf("fsgsbase: fs=%d fs'=%d gs=%#lx\n", old == ref, back == nv, gs1);
      } else printf("fsgsbase: SIGILL\n"); }
}

static void sec_xsave(void) {
    /* xsave / xrstor (x87 + SSE) */
    {
      double a[2] = {1.5, 2.5}, z[2] = {0, 0}, r[2], x = 3.25, y = 0; uint32_t mx = 0x1f80;
      asm volatile("movupd %0,%%xmm1" :: "m"(a) : "xmm1");
      asm volatile("fldl %0" :: "m"(x));
      asm volatile("xsave %0" : "=m"(area) : "a"(3), "d"(0) : "memory");
      asm volatile("movupd %0,%%xmm1" :: "m"(z) : "xmm1");
      asm volatile("fninit");
      asm volatile("xrstor %0" :: "m"(area), "a"(3), "d"(0) : "memory", "xmm1");
      asm volatile("movupd %%xmm1,%0" : "=m"(r)); asm volatile("fstpl %0" : "=m"(y));
      printf("xsave: xmm=%g,%g st0=%g\n", r[0], r[1], y);
      memset(area, 0, 512 + 64);                                         /* all-zero header: every requested component returns to its initial state */
      asm volatile("movupd %0,%%xmm1" :: "m"(a) : "xmm1");
      asm volatile("xrstor %0; ldmxcsr %1" :: "m"(area), "m"(mx), "a"(3), "d"(0) : "memory", "xmm1");
      asm volatile("movupd %%xmm1,%0" : "=m"(r));
      printf("xrstor(init): xmm=%g,%g\n", r[0], r[1]);
      skip = 0; }
}

static void sec_ud(void) {
    /* #UD: SIGILL with si_code ILL_ILLOPN, si_addr and rip at the instruction; the handler may skip it or longjmp out */
    { struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = h; sa.sa_flags = SA_SIGINFO | SA_NODEFER; sigaction(SIGILL, &sa, 0);
      uintptr_t at = 0; skip = 0; sig = 0;
      if (!sigsetjmp(jb, 1)) asm volatile("lea 1f(%%rip),%%rax; mov %%rax,%0; 1: ud2" : "=m"(at) :: "rax");
      printf("ud2: sig=%d code=%d addr=%d rip=%d\n", sig, code, saddr == at, srip == at);
      TRY("invalid06", asm volatile(".byte 0x06"));
      skip = 2; sig = 0; asm volatile("ud2"); printf("ud2 skipped by the handler: sig=%d\n", sig);
      skip = 1; sig = 0; asm volatile(".byte 0x06"); printf("0x06 skipped by the handler: sig=%d\n", sig);
      skip = 2; int n = 0; for (int i = 0; i < 300; i++) { sig = 0; ud2_skip(); n += sig == SIGILL; } printf("ud2 x300: %d\n", n);
      skip = 0; }
}

static void sec_gp(void) {
    /* #GP: SIGSEGV with si_code SI_KERNEL */
    { struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = h; sa.sa_flags = SA_SIGINFO | SA_NODEFER; sigaction(SIGSEGV, &sa, 0);
      sigaction(SIGTRAP, &sa, 0);
      TRY("xsave(mis)", asm volatile("xsave 8(%0)" :: "r"(area), "a"(3), "d"(0) : "memory"));
      TRY("int $0x21", asm volatile("int $0x21"));
      TRY("rdmsr", asm volatile("xor %%ecx,%%ecx; rdmsr" ::: "rax", "rdx", "rcx"));
      TRY("insb", asm volatile("mov $0x3f8,%%edx; insb" ::: "rdx", "memory"));
      TRY("mov cr0", { long v; asm volatile("mov %%cr0,%0" : "=r"(v)); });
      skip = 0;
      TRY("int3", asm volatile(".byte 0xcd,0x03")); }
}

/* Each group runs in its own process: if one of them kills the emulator, the others still report, and the failure names the group. */
static void run_sec(const char*name, void (*fn)(void)) {
    fflush(stdout);
    pid_t p = fork();
    if (p == 0) { fn(); fflush(stdout); _exit(0); }
    int st = 0; waitpid(p, &st, 0);
    if (WIFSIGNALED(st)) printf("section %s: killed by signal %d\n", name, WTERMSIG(st));
    else if (WEXITSTATUS(st)) printf("section %s: exit status %d\n", name, WEXITSTATUS(st));
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    run_sec("loop", sec_loop);
    run_sec("xlat", sec_xlat);
    run_sec("enter", sec_enter);
    run_sec("movbe", sec_movbe);
    run_sec("fsgs", sec_fsgs);
    run_sec("xsave", sec_xsave);
    run_sec("ud", sec_ud);
    run_sec("gp", sec_gp);
    return 0;
}
