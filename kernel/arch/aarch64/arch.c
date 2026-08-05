#include "arch.h"
#include "boot.h"

#include <stdint.h>
#include <stddef.h>

#define PL011_UART_BASE_PHYS 0x09000000

void arch_init(void) {
    uint64_t current_el;
    asm volatile ("mrs %0, CurrentEL" : "=r"(current_el));
    current_el = (current_el >> 2) & 3;
    if (current_el == 2) {
        uint64_t cptr;
        asm volatile ("mrs %0, cptr_el2" : "=r"(cptr));
        cptr &= ~(1ULL << 10); // Clear TFP (Trap FP/SIMD)
        asm volatile ("msr cptr_el2, %0\n\tisb" :: "r"(cptr));
    } else if (current_el == 1) {
        uint64_t cpacr;
        asm volatile ("mrs %0, cpacr_el1" : "=r"(cpacr));
        cpacr |= (3ULL << 20);
        asm volatile ("msr cpacr_el1, %0\n\tisb" :: "r"(cpacr));
    }

    // Map physical MMIO 0-1GB (PL011 UART at 0x09000000) into HHDM page tables as a 1GB Device block
    if (hhdm_request.response != NULL) {
        uint64_t ttbr1;
        asm volatile ("mrs %0, ttbr1_el1" : "=r"(ttbr1));
        uint64_t hhdm = hhdm_request.response->offset;

        uint64_t *l0_table = (uint64_t *) (ttbr1 + hhdm);
        uint64_t l0_entry = l0_table[0];
        if (l0_entry & 1) {
            uint64_t *l1_table = (uint64_t *) ((l0_entry & 0x0000fffffffff000ULL) + hhdm);
            if ((l1_table[0] & 1) == 0) {
                // 1GB block descriptor for physical 0x00000000 with Device-nGnRnE memory attributes (AttrIndx=1)
                l1_table[0] = 0x00000001ULL | (1ULL << 2) | (1ULL << 10) | (3ULL << 8) | (1ULL << 54);
                asm volatile ("tlbi vmalle1is\n\tdsb sy\n\tisb" ::: "memory");
            }
        }
    }
}

void arch_halt(void) {
    for (;;) {
        asm volatile ("msr daifset, #2\n\twfi");
    }
}

void arch_shutdown(void) {
    // PSCI 0.2 SYSTEM_OFF Function ID 0x84000008 via HVC
    register uint64_t x0 asm("x0") = 0x84000008;
    asm volatile ("hvc #0" : : "r"(x0));
    arch_halt();
}
