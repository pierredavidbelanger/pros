#ifndef PROS_ARCH_H
#define PROS_ARCH_H

#if defined(__riscv) && (__riscv_xlen == 32)
    typedef unsigned int uintreg_t;
    #define ARCH_NAME "RISC-V 32-bit (RV32)"
#elif defined(__arm__) || defined(__thumb__)
    typedef unsigned int uintreg_t;
    #define ARCH_NAME "ARM 32-bit (AArch32)"
#else
    #error "Unsupported architecture. Only 32-bit targets (riscv32, arm32) are supported."
#endif

static inline void arch_idle(void) {
#if defined(__riscv) || defined(__arm__)
    asm volatile("wfi");
#endif
}

void uart_putchar(char c);
void uart_puts(const char *s);

void shutdown(void);

#endif // PROS_ARCH_H
