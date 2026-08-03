#include "arch.h"

extern void vector_table(void);

void arch_init(void) {
    // Enable FPU & SIMD in CPACR_EL1
    uint64_t cpacr;
    asm volatile ("mrs %0, cpacr_el1" : "=r"(cpacr));
    cpacr |= (3ULL << 20);
    asm volatile ("msr cpacr_el1, %0\n\tisb" :: "r"(cpacr));
    // Register Vector Base Address Table in VBAR_EL1
    uint64_t vbar = (uint64_t)&vector_table;
    asm volatile ("msr vbar_el1, %0\n\tisb" :: "r"(vbar));
}

void arch_cli(void) {
    asm volatile ("msr daifset, #2"); // Disable IRQ
}

void arch_sti(void) {
    asm volatile ("msr daifclr, #2"); // Enable IRQ
}

void arch_pause(void) {
    asm volatile ("isb");
}

void arch_halt(void) {
    for (;;) {
        asm volatile ("msr daifset, #2\n\twfi");
    }
}
