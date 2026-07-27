#include "uart.h"

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
