#include "arch.h"

#include "kernel.h"

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

struct trap_frame {
    uint32_t regs[31]; // x1 (ra) through x31 (t6)
    uint32_t mepc;
    uint32_t mcause;
    uint32_t mstatus;
};

void trap_handler(struct trap_frame *frame) {
    uint32_t cause = frame->mcause;
    uint32_t epc = frame->mepc;

    kprintf("=== TRAP RECEIVED ===\n");
    kprintf("mcause: 0x%x, mepc: 0x%x\n", cause, epc);

    // If caused by an ecall (environment call / syscall), advance mepc past ecall instruction (4 bytes)
    if (cause == 8 || cause == 11) {
        // Environment call from U-mode or M-mode
        frame->mepc += 4;
    } else {
        kpanic("Unhandled Trap!");
    }
}
