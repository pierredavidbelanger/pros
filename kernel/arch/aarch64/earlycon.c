#include "earlycon.h"
#include "boot.h"

#include <stdint.h>
#include <stddef.h>

#define PL011_UART_BASE_PHYS 0x09000000

// Static L2 page table for early MMIO mapping (2MB granule, aligned to 4096 bytes)
static uint64_t early_l2_table[512] __attribute__((aligned(4096)));

void earlycon_init(void) {
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

void earlycon_putc(char c) {
    uintptr_t base = PL011_UART_BASE_PHYS;
    if (hhdm_request.response != NULL) {
        base += hhdm_request.response->offset;
    }
    volatile uint32_t *uart_dr = (volatile uint32_t *)base;
    *uart_dr = (uint32_t)(unsigned char)c;
}
