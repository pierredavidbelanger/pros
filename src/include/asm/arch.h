#ifndef PROS_ARCH_H
#define PROS_ARCH_H
#include <stdint.h>

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

uintptr_t uart_addr();
void uart_putchar(char c);
void uart_puts(const char *s);

void shutdown(void);

uintptr_t *vmm_create_root_table(void);
void vmm_map_page(uintptr_t *root_pt, uintptr_t vaddr, uintptr_t paddr, uint8_t r, uint8_t w, uint8_t x);
void vmm_enable(uintptr_t *root_pt);

#endif // PROS_ARCH_H
