#include "arch.h"

#include "kernel.h"

#include <stdint.h>

/* QEMU ARM Virt board maps PL011 UART to 0x09000000 */
#define PL011_BASE 0x09000000
#define UART_DR    ((volatile unsigned int*)(PL011_BASE + 0x00))
#define UART_FR    ((volatile unsigned int*)(PL011_BASE + 0x18))

uintptr_t uart_addr() {
    return PL011_BASE;
}

void uart_putchar(const char c) {
    /* Wait until Transmit FIFO is not full (bit 5 in Flag Register) */
    while (*UART_FR & (1 << 5)) {
    }
    *UART_DR = c;
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putchar(*s++);
    }
}

// PSCI function ID for SYSTEM_OFF (PSCI 0.2+)
#define PSCI_SYSTEM_OFF 0x84000008

void shutdown(void) {
    __asm__ __volatile__(
        "mov r0, %0\n"
        "smc #0\n"
        :
        : "r"(PSCI_SYSTEM_OFF)
        : "r0"
    );
}

struct trap_frame {
    uint32_t r[13]; // r0 - r12
    uint32_t lr;
};

void trap_handler(struct trap_frame *frame) {
    kprintf("=== ARM TRAP RECEIVED ===\n");
    kprintf("old r0 contains 0x%x\n", frame->r[0]);
    kprintf("trap_handler is at 0x%x\n", frame);
    kprintf("Returned to address (LR): 0x%x\n", frame->lr);
}

uintptr_t *vmm_create_root_table(void) {
    return 0;
}

void vmm_map_page(uintptr_t *root_pt, uintptr_t vaddr, uintptr_t paddr, uint8_t r, uint8_t w, uint8_t x) {
}

void vmm_enable(uintptr_t *root_pt) {
}
