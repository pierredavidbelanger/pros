#include "arch/arch.h"

#include "mm/vmm.h"

void arch_init(void) {
    // Enable FPU & SIMD in EL1 / EL2
    uint64_t current_el;
    asm volatile ("mrs %0, CurrentEL" : "=r"(current_el));
    current_el = (current_el >> 2) & 3;
    if (current_el == 2) {
        uint64_t cptr;
        asm volatile ("mrs %0, cptr_el2" : "=r"(cptr));
        cptr &= ~(1ULL << 10);
        asm volatile ("msr cptr_el2, %0\n\tisb" :: "r"(cptr));
    } else if (current_el == 1) {
        uint64_t cpacr;
        asm volatile ("mrs %0, cpacr_el1" : "=r"(cpacr));
        cpacr |= (3ULL << 20);
        asm volatile ("msr cpacr_el1, %0\n\tisb" :: "r"(cpacr));
    }



    // Enable & configure TTBR0_EL1 in TCR_EL1 (Inner Shareable, WB Cacheable)
    uint64_t tcr;
    asm volatile ("mrs %0, tcr_el1" : "=r"(tcr));
    tcr &= ~(1ULL << 7); // EPD0 = 0 (TTBR0_EL1 Enabled)
    tcr |= (3ULL << 12); // SH0 = 0b11 (Inner Shareable)
    tcr |= (1ULL << 10); // ORGN0 = 0b01 (Write-Back Cacheable)
    tcr |= (1ULL << 8);  // IRGN0 = 0b01 (Write-Back Cacheable)
    asm volatile ("msr tcr_el1, %0\n\tisb" :: "r"(tcr) : "memory");

    // Install exception vector table
    extern char vector_table[];
    asm volatile ("msr vbar_el1, %0\n\tisb" :: "r"(vector_table) : "memory");
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
    // Fallback: halt if poweroff is not supported by hardware
    arch_halt();
}

// ─── VMM Architecture Primitives ─────────────────────────────

uint64_t arch_vmm_make_pte(uint64_t phys_addr, uint64_t vmm_flags, bool is_table) {
    uint64_t pte = phys_addr & 0x0000FFFFFFFFF000ULL;

    if (is_table) {
        // Table descriptor: bits [1:0] = 0b11 (Valid + Table)
        pte |= 0x3ULL;
        return pte;
    }

    // Page descriptor (leaf, 4 KiB): bits [1:0] = 0b11 (Valid + Page)
    pte |= 0x3ULL;  // Valid (bit 0) + Page (bit 1)

    // Access Flag (bit 10) — MUST be set
    pte |= (1ULL << 10);

    // Inner Shareable (bits [9:8] = 0b11)
    pte |= (3ULL << 8);

    // Memory attributes: AttrIndx[2:0] (bits [4:2])
    if (vmm_flags & VMM_CACHE_DISABLE) {
        pte |= (1ULL << 2);  // AttrIndx = 1 (Device-nGnRnE)
    }

    // Access Permissions: AP[2:1] at bits [7:6]
    if (vmm_flags & VMM_USER) {
        pte |= (1ULL << 6);  // AP[1] = 1 -> EL0 access
    }
    if (!(vmm_flags & VMM_WRITABLE)) {
        pte |= (1ULL << 7);  // AP[2] = 1 -> Read-Only
    }

    // Execute-Never bits
    if (vmm_flags & VMM_NO_EXECUTE) {
        pte |= (1ULL << 54);  // UXN
        pte |= (1ULL << 53);  // PXN
    }

    return pte;
}

bool arch_vmm_pte_is_present(uint64_t pte) {
    return (pte & 0x1ULL) != 0;
}

uint64_t arch_vmm_pte_get_phys(uint64_t pte) {
    return pte & 0x0000FFFFFFFFF000ULL;
}

uint64_t arch_vmm_get_kernel_root(void) {
    uint64_t val;
    asm volatile ("mrs %0, ttbr1_el1" : "=r"(val));
    return val & 0x0000FFFFFFFFFFFEULL;
}

void arch_vmm_set_kernel_root(uint64_t phys_addr) {
    asm volatile ("msr ttbr1_el1, %0\n\tisb" :: "r"(phys_addr) : "memory");
}

uint64_t arch_vmm_get_user_root(void) {
    uint64_t val;
    asm volatile ("mrs %0, ttbr0_el1" : "=r"(val));
    return val & 0x0000FFFFFFFFFFFEULL;
}

void arch_vmm_set_user_root(uint64_t phys_addr) {
    asm volatile ("msr ttbr0_el1, %0\n\tdsb ish\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" :: "r"(phys_addr) : "memory");
}

uint64_t arch_vmm_get_fault_addr(void) {
    uint64_t val;
    asm volatile ("mrs %0, far_el1" : "=r"(val));
    return val;
}

void arch_vmm_invlpg(void *virt_addr) {
    (void)virt_addr;
    asm volatile ("dsb ish\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
}
