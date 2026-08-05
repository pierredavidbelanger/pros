#include "arch.h"
#include "boot.h"

#include <stdint.h>
#include <stddef.h>

#define PL011_UART_BASE_PHYS 0x09000000

static volatile uint32_t *pl011_reg(size_t offset) {
    uintptr_t base = PL011_UART_BASE_PHYS;
    if (hhdm_request.response != NULL) {
        base += hhdm_request.response->offset;
    }
    return (volatile uint32_t *) (base + offset);
}

void arch_putc(char c) {
    volatile uint32_t *uart_dr = pl011_reg(0x00);
    *uart_dr = (uint32_t)(unsigned char)c;
}
