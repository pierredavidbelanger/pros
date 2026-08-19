#include "core/serial.h"

#include "gic.h"
#include "core/boot.h"

#include "stdc.h"

#define PL011_UART_BASE_PHYS 0x09000000

#define UARTDR   0x00 // data register
#define UARTFR   0x18 // flags: bit 4 is RXFE, "receive FIFO empty"
#define UARTIMSC 0x38 // interrupt mask set/clear: bit 4 is the receive interrupt
#define UARTICR  0x44 // interrupt clear: bit 4 clears a pending receive interrupt

// Static L2 page table for early MMIO mapping (2MB granule, aligned to 4096 bytes)
static uint64_t early_l2_table[512] __attribute__((aligned(4096)));

// same base+hhdm dance serial_putc does today, pulled out so getc/init can share it
static volatile uint32_t *serial_reg(uint64_t offset) {
    uintptr_t base = PL011_UART_BASE_PHYS + offset;
    if (hhdm_request.response != NULL) {
        base += hhdm_request.response->offset;
    }
    return (volatile uint32_t *) base;
}

void serial_init_put(void) {
    // Map 2MB block at 0x08000000 (containing PL011 UART at 0x09000000) into HHDM
    if (hhdm_request.response != NULL && executable_address_request.response != NULL) {
        uint64_t ttbr1;
        asm volatile ("mrs %0, ttbr1_el1" : "=r"(ttbr1));
        uint64_t hhdm = hhdm_request.response->offset;
        uint64_t virt_base = executable_address_request.response->virtual_base;
        uint64_t phys_base = executable_address_request.response->physical_base;

        uint64_t *l0_table = (uint64_t *)(ttbr1 + hhdm);
        uint64_t l0_entry = l0_table[0];
        if (l0_entry & 1) {
            uint64_t *l1_table = (uint64_t *)((l0_entry & 0x0000fffffffff000ULL) + hhdm);
            if ((l1_table[0] & 1) == 0) {
                // Get exact physical address of static early_l2_table
                uint64_t l2_phys = (uintptr_t)early_l2_table - virt_base + phys_base;

                // 1. Point L1[0] to early_l2_table (Table Descriptor)
                l1_table[0] = l2_phys | 0x3ULL;

                // 2. Map ONLY physical 0x08000000 (2MB region covering PL011 UART) as a 2MB Block Descriptor
                size_t l2_idx = PL011_UART_BASE_PHYS >> 21; // index 72
                uint64_t block_phys = PL011_UART_BASE_PHYS & ~0x1FFFFFULL; // 0x08000000
                early_l2_table[l2_idx] = block_phys | 0x01ULL | (1ULL << 2) | (1ULL << 10) | (3ULL << 8) | (1ULL << 54);

                asm volatile ("tlbi vmalle1is\n\tdsb sy\n\tisb" ::: "memory");
            }
        }
    }
}

void serial_init_get(void) {
    *serial_reg(UARTIMSC) |= (1u << 4); // enable RX interrupt at the UART itself
    gic_enable_irq(GIC_INTID_PL011);     // GIC must already be up
}

void serial_putc(char c) {
    *serial_reg(UARTDR) = (uint32_t) c;
}

// true while the RX FIFO still has an unread byte,
// RXRIS does not re-latch on its own while bytes remain queued,
bool serial_rx_ready(void) {
    return (*serial_reg(UARTFR) & (1u << 4)) == 0;
}

char serial_getc(void) {
    uint8_t byte = (uint8_t)(*serial_reg(UARTDR) & 0xFF);
    *serial_reg(UARTICR) = (1u << 4); // write-only register, RMW would read back UNPREDICTABLE
    return (char) byte;
}
