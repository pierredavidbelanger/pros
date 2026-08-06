#ifndef PROS_ARCH_H
#define PROS_ARCH_H

#include <stdint.h>
#include <stdbool.h>

void arch_init(void);

void arch_putc(char c);

void arch_halt(void);

// Cleanly power off the machine
void arch_shutdown(void);



// ─── VMM Architecture Primitives ─────────────────────────────

// Build a hardware PTE from physical address + abstract VMM flags.
// Set is_table=true for intermediate (non-leaf) table descriptors.
uint64_t arch_vmm_make_pte(uint64_t phys_addr, uint64_t vmm_flags, bool is_table);

// Test whether a hardware PTE is valid/present
bool arch_vmm_pte_is_present(uint64_t pte);

// Extract the physical address from a hardware PTE (mask off flag bits)
uint64_t arch_vmm_pte_get_phys(uint64_t pte);

// Get/set the physical base address of root page tables
uint64_t arch_vmm_get_kernel_root(void);
void     arch_vmm_set_kernel_root(uint64_t phys_addr);
uint64_t arch_vmm_get_user_root(void);
void     arch_vmm_set_user_root(uint64_t phys_addr);

// Read faulting virtual address during Page Fault / Data Abort
uint64_t arch_vmm_get_fault_addr(void);

// Invalidate TLB entry for a specific virtual address
void arch_vmm_invlpg(void *virt_addr);

#endif //PROS_ARCH_H
