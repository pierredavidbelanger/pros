#include "arch.h"

void arch_init(void) {
    // Enable FPU & SIMD in EL1 / EL2
    uint64_t current_el;
    asm volatile ("mrs %0, CurrentEL" : "=r"(current_el));
    current_el = (current_el >> 2) & 3;
    if (current_el == 2) {
        uint64_t cptr;
        asm volatile ("mrs %0, cptr_el2" : "=r"(cptr));
        cptr &= ~(1ULL << 10);
        asm volatile ("msr cptr_el2, %0\n\tisb" :: "r"(cptr));
    } else if (current_el == 1) {
        uint64_t cpacr;
        asm volatile ("mrs %0, cpacr_el1" : "=r"(cpacr));
        cpacr |= (3ULL << 20);
        asm volatile ("msr cpacr_el1, %0\n\tisb" :: "r"(cpacr));
    }
}

void arch_halt(void) {
    for (;;) {
        asm volatile ("msr daifset, #2\n\twfi");
    }
}

void arch_shutdown(void) {
    // PSCI 0.2 SYSTEM_OFF Function ID 0x84000008 via HVC
    register uint64_t x0 asm("x0") = 0x84000008;
    asm volatile ("hvc #0" : : "r"(x0));
    arch_halt();
}
