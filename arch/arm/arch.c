#include "arch.h"

/* QEMU ARM Virt board maps PL011 UART to 0x09000000 */
#define PL011_BASE 0x09000000
#define UART_DR    ((volatile unsigned int*)(PL011_BASE + 0x00))
#define UART_FR    ((volatile unsigned int*)(PL011_BASE + 0x18))

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
