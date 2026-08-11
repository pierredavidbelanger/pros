#include "arch/arch.h"

#include "idt.h"
#include "io.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "core/memory.h"

void arch_init(void) {
    idt_init();
}

void arch_halt(void) {
    for (;;) {
        asm volatile ("cli; hlt");
    }
}

void arch_shutdown(void) {
    // QEMU q35 fallback port
    outw(0x604, 0x2000);
    // Fallback: QEMU debug-exit port
    outw(0x501, 0x00);
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

void arch_vmm_set_user_root(uint64_t phys_addr) {
    // x86_64 has a single CR3 for both kernel and user mappings.
    asm volatile ("mov %0, %%cr3" :: "r"(phys_addr) : "memory");
}

uint64_t arch_vmm_ensure_user_root(void) {
    // Limine always leaves a valid CR3 in place on x86_64 — nothing to do.
    return arch_vmm_get_kernel_root();
}

uint64_t arch_vmm_new_context_root(uint64_t kernel_root_phys) {
    uint64_t root_phys = pmm_alloc(1);
    if (!root_phys) return 0;

    uint64_t *root_virt = pmm_phys_to_virt(root_phys);
    memset(root_virt, 0, PAGE_SIZE);

    // x86_64 has a single root (CR3) shared by kernel and user mappings: every new context
    // must clone the kernel's upper-half entries (256..511) so kernel code/data stays mapped
    // whenever this context is active.
    uint64_t *kernel_root_virt = pmm_phys_to_virt(kernel_root_phys);
    for (size_t i = 256; i < 512; i++) {
        root_virt[i] = kernel_root_virt[i];
    }

    return root_phys;
}

// #PF error code: bit 0 = present (0 = not-present fault, 1 = protection violation),
// bit 1 = write access.

bool arch_vmm_fault_is_present(uint64_t fault_code) {
    return (fault_code & (1ULL << 0)) != 0;
}

bool arch_vmm_fault_is_write(uint64_t fault_code) {
    return (fault_code & (1ULL << 1)) != 0;
}

void arch_vmm_invlpg(void *virt_addr) {
    asm volatile ("invlpg (%0)" :: "r"(virt_addr) : "memory");
}
