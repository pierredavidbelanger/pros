#include "arch/arch.h"

#include "gic.h"
#include "timer.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "core/memory.h"

void arch_init(void) {
    // Enable FPU & SIMD access at EL1 (and EL0, once user space exists).
    // Limine always drop to EL1 before jumping to the kernel
    uint64_t cpacr;
    asm volatile ("mrs %0, cpacr_el1" : "=r"(cpacr));
    cpacr |= (3ULL << 20); // FPEN = 0b11: don't trap FP/SIMD instructions at EL0 or EL1
    asm volatile ("msr cpacr_el1, %0\n\tisb" :: "r"(cpacr));

    // Define what arch_vmm_make_pte() AttrIndx bits actually mean, instead of silently getting what limine set MAIR_EL1 to.
    // Index 0: Normal, Write-Back Cacheable (AttrIndx = 0)
    // Index 1: Device-nGnRnE (AttrIndx = 1, VMM_CACHE_DISABLE)
    uint64_t mair = (0xFFULL << 0) | (0x00ULL << 8);
    asm volatile ("msr mair_el1, %0\n\tisb" :: "r"(mair) : "memory");

    // Enable & configure TTBR0_EL1 in TCR_EL1 (Inner Shareable, WB Cacheable)
    uint64_t tcr;
    asm volatile ("mrs %0, tcr_el1" : "=r"(tcr));
    tcr &= ~(1ULL << 7); // EPD0 = 0 (TTBR0_EL1 Enabled)
    tcr |= (3ULL << 12); // SH0 = 0b11 (Inner Shareable)
    tcr |= (1ULL << 10); // ORGN0 = 0b01 (Write-Back Cacheable)
    tcr |= (1ULL << 8);  // IRGN0 = 0b01 (Write-Back Cacheable)
    asm volatile ("msr tcr_el1, %0\n\tisb" :: "r"(tcr) : "memory");

    // Move the kernel off SP_EL0 and onto SP_EL1, where it belongs.
    uint64_t stack_ptr;
    asm volatile (
        "mov %0, sp\n\t"    // the live stack pointer, still SP_EL0 at this point
        "msr spsel, #1\n\t" // sp now names SP_EL1
        "mov sp, %0"        // same value back in: nothing on the stack moved
        : "=&r"(stack_ptr) :: "memory");

    // Poison SP_EL0
    // Nothing should ever run at EL1t again, and a path that does will fault on its first push
    asm volatile ("msr sp_el0, xzr");

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

// VMM

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

void arch_vmm_set_user_root(uint64_t phys_addr) {
    asm volatile ("msr ttbr0_el1, %0\n\tdsb ish\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" :: "r"(phys_addr) : "memory");
}

uint64_t arch_vmm_ensure_user_root(void) {
    uint64_t root_phys;
    asm volatile ("mrs %0, ttbr0_el1" : "=r"(root_phys));
    root_phys &= 0x0000FFFFFFFFFFFEULL;
    if (root_phys != 0) {
        return root_phys;
    }
    // Limine leaves TTBR0_EL1 unset on AArch64 — install a fresh, blank root table.
    root_phys = pmm_alloc(1);
    if (!root_phys) return 0;
    uint64_t *root_virt = pmm_phys_to_virt(root_phys);
    memset(root_virt, 0, PAGE_SIZE);
    arch_vmm_set_user_root(root_phys);
    return root_phys;
}

uint64_t arch_vmm_new_context_root(uint64_t kernel_root_phys) {
    // AArch64 keeps the kernel's mappings in TTBR1_EL1, entirely separate from a context's
    // own TTBR0_EL1 root — nothing to clone from kernel_root_phys.
    (void)kernel_root_phys;
    uint64_t root_phys = pmm_alloc(1);
    if (!root_phys) return 0;
    uint64_t *root_virt = pmm_phys_to_virt(root_phys);
    memset(root_virt, 0, PAGE_SIZE);
    return root_phys;
}

// fault_code is the raw ESR_EL1 value. ISS[5:0] is the Data/Instruction Fault Status Code
// (DFSC): a Translation fault (entry not present at some level, encoded 0b0000LL) is the
// only case treated as a legitimate not-present fault — permission faults, address-size
// faults, etc. are all real violations. ISS bit 6 (WnR) reports a write access.

bool arch_vmm_fault_is_present(uint64_t fault_code) {
    uint32_t dfsc = fault_code & 0x3FULL;
    bool is_translation_fault = (dfsc & 0x3CULL) == 0x04ULL;
    return !is_translation_fault;
}

bool arch_vmm_fault_is_write(uint64_t fault_code) {
    return (fault_code & (1ULL << 6)) != 0;
}

void arch_vmm_invlpg(void *virt_addr) {
    (void)virt_addr;
    asm volatile ("dsb ish\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
}

// Timer & Interrupt

void arch_timer_init(uint32_t hz) {
    // The controller first: the timer's output must have somewhere to go before it is unmasked.
    gic_init();
    arm_timer_init(hz);
}

void arch_irq_enable(void) {
    asm volatile ("msr daifclr, #2" ::: "memory");
}

void arch_irq_disable(void) {
    asm volatile ("msr daifset, #2" ::: "memory");
}
