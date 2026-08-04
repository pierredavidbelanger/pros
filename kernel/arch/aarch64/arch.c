#include "arch.h"

extern void vector_table(void);

void arch_init(void) {
    // Enable FPU & SIMD in CPACR_EL1
    uint64_t cpacr;
    asm volatile ("mrs %0, cpacr_el1" : "=r"(cpacr));
    cpacr |= (3ULL << 20);
    asm volatile ("msr cpacr_el1, %0\n\tisb" :: "r"(cpacr));
    // Register Vector Base Address Table in VBAR_EL1
    uint64_t vbar = (uint64_t) & vector_table;
    asm volatile ("msr vbar_el1, %0\n\tisb" :: "r"(vbar));
}

void arch_cli(void) {
    asm volatile ("msr daifset, #2"); // Disable IRQ
}

void arch_sti(void) {
    asm volatile ("msr daifclr, #2"); // Enable IRQ
}

void arch_pause(void) {
    asm volatile ("isb");
}

void arch_halt(void) {
    for (;;) {
        asm volatile ("msr daifset, #2\n\twfi");
    }
}

// Architecture VMM impl

uint64_t arch_vmm_get_root(void) {
    uint64_t val;
    asm volatile ("mrs %0, ttbr0_el1" : "=r"(val));
    return val;
}

void arch_vmm_set_root(uint64_t phys_addr) {
    asm volatile ("msr ttbr0_el1, %0\n\tisb" :: "r"(phys_addr) : "memory");
}

uint64_t arch_vmm_get_fault_addr(void) {
    uint64_t val;
    asm volatile ("mrs %0, far_el1" : "=r"(val));
    return val;
}

void arch_vmm_invlpg(void *virt_addr) {
    uint64_t page = ((uint64_t) virt_addr) >> 12;
    asm volatile ("tlbi vaae1is, %0\n\tdsb ish\n\tisb" :: "r"(page) : "memory");
}

bool arch_vmm_get_flags(uint64_t pte, uint64_t flags) {
    if ((flags & VMM_PRESENT) && !(pte & (1ULL << 0)))
        return false;
    // AP[2] (bit 7) == 0 means writable
    if ((flags & VMM_WRITABLE) && (pte & (1ULL << 7)))
        return false;
    // AP[1] (bit 6) == 1 means user accessible
    if ((flags & VMM_USER) && !(pte & (1ULL << 6)))
        return false;
    // UXN (bit 54) or PXN (bit 53)
    if ((flags & VMM_NO_EXECUTE) && ((pte & ((1ULL << 53) | (1ULL << 54))) == 0))
        return false;
    // AttrIndx[2:0] (bits 4:2)
    if ((flags & VMM_CACHE_DISABLE) && ((pte & (7ULL << 2)) != (1ULL << 2)))
        return false;
    return true;
}

uint64_t arch_vmm_set_flags(uint64_t pte, uint64_t flags) {
    if (flags & VMM_PRESENT) {
        // Valid (bit 0) + Page/Table (bit 1) + Inner Shareable (bit 8 and 9) + Access Flag (bit 10)
        pte |= (1ULL << 0) | (1ULL << 1) | (3ULL << 8) | (1ULL << 10);
    }
    if (!(flags & VMM_WRITABLE)) {
        // Set AP[2] (bit 7) to 1 for Read-Only if not writable
        pte |= (1ULL << 7);
    }
    if (flags & VMM_USER) {
        // AP[1] (bit 6) = 1 for User access
        pte |= (1ULL << 6);
    }
    if (flags & VMM_NO_EXECUTE) {
        // Set both PXN (bit 53) and UXN (bit 54)
        pte |= (1ULL << 53) | (1ULL << 54);
    }
    if (flags & VMM_CACHE_DISABLE) {
        // Set MAIR index 1 (Device / Uncached)
        pte |= (1ULL << 2);
    }
    return pte;
}
