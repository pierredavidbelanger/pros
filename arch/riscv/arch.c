#include "arch.h"

#include <stdint.h>

/* QEMU RISC-V Virt board maps 16550A UART to 0x10000000 */
#define UART_BASE 0x10000000
#define UART_THR  ((volatile unsigned char*)(UART_BASE + 0))
#define UART_LSR  ((volatile unsigned char*)(UART_BASE + 5))

void uart_putchar(const char c) {
    while ((*UART_LSR & (1 << 5)) == 0);
    *UART_THR = c;
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putchar(*s++);
    }
}

#define QEMU_RISCV_POWEROFF_ADDR 0x100000
#define QEMU_RISCV_PASS          0x5555
#define QEMU_RISCV_FAIL          0x3333

void shutdown(void) {
    // Write 0x5555 to 0x100000 to cleanly exit QEMU with code 0
    *(volatile uint32_t *) QEMU_RISCV_POWEROFF_ADDR = QEMU_RISCV_PASS;
}
