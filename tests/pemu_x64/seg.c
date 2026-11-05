/* mov r/m, Sreg and mov Sreg, r/m: the selector values of a Linux x86_64 process, zero extension, and that loading is accepted. */
#include <stdio.h>
#include <stdint.h>

int main(void) {
    uint16_t m[6]; uint64_t r64, r32, r16;
    __asm__ volatile("movw %%es, %0" : "=m"(m[0]));
    __asm__ volatile("movw %%cs, %0" : "=m"(m[1]));
    __asm__ volatile("movw %%ss, %0" : "=m"(m[2]));
    __asm__ volatile("movw %%ds, %0" : "=m"(m[3]));
    __asm__ volatile("movw %%fs, %0" : "=m"(m[4]));
    __asm__ volatile("movw %%gs, %0" : "=m"(m[5]));
    printf("es=%x cs=%x ss=%x ds=%x fs=%x gs=%x\n", m[0], m[1], m[2], m[3], m[4], m[5]);
    r64 = ~0ULL; __asm__ volatile("mov %%cs, %%rax" : "+a"(r64));
    r32 = ~0ULL; __asm__ volatile("mov %%cs, %%eax" : "+a"(r32));
    r16 = ~0ULL; __asm__ volatile("mov %%cs, %%ax" : "+a"(r16));
    printf("rax=%llx eax=%llx ax=%llx\n", (unsigned long long) r64, (unsigned long long) r32, (unsigned long long) r16);
    uint16_t z = 0;
    __asm__ volatile("mov %0, %%ds" :: "r"(z));
    __asm__ volatile("movw %0, %%es" :: "m"(z));
    __asm__ volatile("movw %%ds, %0" : "=m"(m[3]));
    printf("after load ds=%x\n", m[3]);
    return 0;
}
