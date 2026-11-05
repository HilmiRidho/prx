/* iretq in 64-bit user mode, as Wine's syscall dispatcher uses it to resume user code:
 * build the 5-word frame (rip, cs, rflags, rsp, ss) by hand and return through it. */
#include <stdio.h>
#include <stdint.h>

int main(void) {
    uint64_t out = 0, flags_after = 0;
    __asm__ volatile(
        "movq %%rsp, %%rbx\n\t"            /* remember the real stack pointer */
        "movq %%ss, %%rax\n\t"  "pushq %%rax\n\t"      /* ss */
        "pushq %%rbx\n\t"                              /* rsp to resume with */
        "pushfq\n\t"                                   /* rflags */
        "movq %%cs, %%rax\n\t"  "pushq %%rax\n\t"      /* cs */
        "leaq 1f(%%rip), %%rax\n\t" "pushq %%rax\n\t"  /* rip */
        "iretq\n\t"
        "movq $0xbad, %0\n\t" "jmp 2f\n\t"
        "1: movq $0x600d, %0\n\t"
        "2: pushfq\n\t" "popq %1\n\t"
        : "=r"(out), "=r"(flags_after) :: "rax", "rbx", "memory", "cc");
    printf("iretq %s\n", out == 0x600d ? "ok" : "fail");
    printf("if %s\n", (flags_after & 0x200) ? "set" : "clear");
    return 0;
}
