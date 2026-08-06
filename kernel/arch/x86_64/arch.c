#include "arch/arch.h"
#include "idt.h"
#include "mm/vmm.h"
#include "drivers/acpi/acpi.h"

void arch_init(void) {
    idt_init();
}

void arch_halt(void) {
    for (;;) {
        asm volatile ("cli; hlt");
    }
}

void arch_outw(uint16_t port, uint16_t val) {
    asm volatile ("outw %0, %1" : : "a"(val), "d"(port));
}

void arch_shutdown(void) {
    // Dynamic ACPI FADT PM1a poweroff
    acpi_shutdown();
    // QEMU q35 fallback port
    arch_outw(0x604, 0x2000);
    // Fallback: QEMU debug-exit port
    arch_outw(0x501, 0x00);
    // Fallback: halt if poweroff is not supported by hardware
    arch_halt();
}

// ─── VMM Architecture Primitives ─────────────────────────────

uint64_t arch_vmm_make_pte(uint64_t phys_addr, uint64_t vmm_flags, bool is_table) {
    // x86_64: same bit layout for table and page entries (no is_table distinction needed)
    (void)is_table;
    uint64_t pte = phys_addr & ~0xFFFULL;
    if (vmm_flags & VMM_PRESENT)       pte |= (1ULL << 0);
    if (vmm_flags & VMM_WRITABLE)      pte |= (1ULL << 1);
    if (vmm_flags & VMM_USER)          pte |= (1ULL << 2);
    if (vmm_flags & VMM_CACHE_DISABLE) pte |= (1ULL << 4);
    if (vmm_flags & VMM_NO_EXECUTE)    pte |= (1ULL << 63);
    return pte;
}

bool arch_vmm_pte_is_present(uint64_t pte) {
    return (pte & (1ULL << 0)) != 0;
}

uint64_t arch_vmm_pte_get_phys(uint64_t pte) {
    return pte & 0x000FFFFFFFFFF000ULL;
}

// x86_64 has a single CR3 for both kernel and user mappings
uint64_t arch_vmm_get_kernel_root(void) {
    uint64_t val;
    asm volatile ("mov %%cr3, %0" : "=r"(val));
    return val;
}

void arch_vmm_set_kernel_root(uint64_t phys_addr) {
    asm volatile ("mov %0, %%cr3" :: "r"(phys_addr) : "memory");
}

uint64_t arch_vmm_get_user_root(void) {
    return arch_vmm_get_kernel_root();
}

void arch_vmm_set_user_root(uint64_t phys_addr) {
    arch_vmm_set_kernel_root(phys_addr);
}

uint64_t arch_vmm_get_fault_addr(void) {
    uint64_t val;
    asm volatile ("mov %%cr2, %0" : "=r"(val));
    return val;
}

void arch_vmm_invlpg(void *virt_addr) {
    asm volatile ("invlpg (%0)" :: "r"(virt_addr) : "memory");
}
