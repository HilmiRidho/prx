/* Things Wine's 64-bit ntdll does at start-up: TEB through %gs, arch_prctl queries, and the
 * syscall-user-dispatch probe, which the emulator must refuse (it would act on the host). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>

int main(void) {
    static uint64_t teb[16];
    uint64_t v = 0, got = 0;
    teb[6] = 0x1122334455667788ULL;                       /* gs:[0x30], the TEB self pointer slot */
    teb[0] = 0xdeadbeefULL;
    long r = syscall(SYS_arch_prctl, 0x1001 /*ARCH_SET_GS*/, (unsigned long) teb);
    printf("set_gs %s\n", r == 0 ? "ok" : "fail");
    __asm__ volatile("movq %%gs:0x30, %0" : "=r"(v));
    printf("gs:0x30 %llx\n", (unsigned long long) v);
    __asm__ volatile("movq $0x42, %%gs:0x38" ::: "memory");
    printf("store via gs %llx\n", (unsigned long long) teb[7]);
    r = syscall(SYS_arch_prctl, 0x1004 /*ARCH_GET_GS*/, (unsigned long) &got);
    printf("get_gs %s\n", (r == 0 && got == (uint64_t)(unsigned long) teb) ? "ok" : "fail");
    r = syscall(SYS_prctl, 59 /*PR_SET_SYSCALL_USER_DISPATCH*/, 1, 0, 0, 0);
    printf("sud %s\n", (r == -1 && errno == EINVAL) ? "EINVAL" : "other");
    r = syscall(SYS_arch_prctl, 0x1011 /*ARCH_GET_CPUID*/, 0);
    printf("get_cpuid %ld\n", r);
    return 0;
}
