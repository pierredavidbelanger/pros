#ifndef PROS_ARCH_H
#define PROS_ARCH_H

#include <stdint.h>
#include <stdbool.h>

void arch_init(void);

// Disable CPU interrupts.
void arch_cli(void);

// Enable CPU interrupts.
void arch_sti(void);

// Low-power spin/pause hint for tight loops.
void arch_pause(void);

// Halt the CPU in an idle loop.
void arch_halt(void);

// Architecture VMM abstractions

#define VMM_PRESENT       (1ULL << 0)
#define VMM_WRITABLE      (1ULL << 1)
#define VMM_USER          (1ULL << 2)
#define VMM_NO_EXECUTE    (1ULL << 3)
#define VMM_CACHE_DISABLE (1ULL << 4)

// Returns physical base address of current active root page table
uint64_t arch_vmm_get_root(void);
// Sets physical base address of active root page table
void arch_vmm_set_root(uint64_t phys_addr);
// Returns faulting virtual address during Page Fault
uint64_t arch_vmm_get_fault_addr(void);
// Invalidates TLB entry for specific virtual address
void arch_vmm_invlpg(void *virt_addr);
// Get flags from an addr
bool arch_vmm_get_flags(uint64_t pte, uint64_t flags);
// Add flags to an addr
uint64_t arch_vmm_set_flags(uint64_t pte, uint64_t flags);

#endif //PROS_ARCH_H
