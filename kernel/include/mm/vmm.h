#ifndef PROS_VMM_H
#define PROS_VMM_H

#include "stdc.h"

// ─── Abstract VMM Flags (architecture-neutral) ───────────────
// These are NOT hardware PTE bits. They are translated to hardware
// format by arch_vmm_make_pte() in each arch's arch.c.

#define VMM_PRESENT       (1ULL << 0)
#define VMM_WRITABLE      (1ULL << 1)
#define VMM_USER          (1ULL << 2)
#define VMM_CACHE_DISABLE (1ULL << 3)
#define VMM_NO_EXECUTE    (1ULL << 4)

// ─── VMM Context ─────────────────────────────────────────────

struct vmm_context {
    uint64_t root_phys;   // Physical address of root page table
    uint64_t *root_virt;  // HHDM virtual address of root page table
};

extern struct vmm_context *vmm_kernel_context;
extern struct vmm_context *vmm_current_context;

// ─── VMM API ─────────────────────────────────────────────────

// Initialize VMM: capture Limine's page tables as kernel context
void vmm_init(void);

// Create a new address space (clones kernel upper-half entries)
struct vmm_context *vmm_create_context(void);

// Destroy an address space (frees root table + context struct)
void vmm_destroy_context(struct vmm_context *ctx);

// Map a single 4 KiB page: virt_addr → phys_addr with given flags
int vmm_map_page(struct vmm_context *ctx, uint64_t virt_addr,
                 uint64_t phys_addr, uint64_t flags);

// Unmap a single 4 KiB page
int vmm_unmap_page(struct vmm_context *ctx, uint64_t virt_addr);

// Translate virtual address to physical (returns 0 if not mapped)
uint64_t vmm_virt_to_phys(struct vmm_context *ctx, uint64_t virt_addr);

// Switch active address space
void vmm_switch_context(struct vmm_context *ctx);

// Handle a page fault (demand paging). Returns true if resolved.
bool vmm_handle_page_fault(uint64_t fault_addr, uint64_t error_code);

#endif //PROS_VMM_H
