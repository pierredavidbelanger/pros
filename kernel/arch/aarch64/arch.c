#include "arch.h"

#include <stdint.h>

void arch_init() {
    // Enable FPU & SIMD in CPACR_EL1
    uint64_t cpacr;
    asm volatile ("mrs %0, cpacr_el1" : "=r"(cpacr));
    cpacr |= (3ULL << 20);
    asm volatile ("msr cpacr_el1, %0\n\tisb" :: "r"(cpacr));
}
