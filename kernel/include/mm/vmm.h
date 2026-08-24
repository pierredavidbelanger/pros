#ifndef PROS_VMM_H
#define PROS_VMM_H

#include "stdc.h"

// ─── Abstract VMM Flags (architecture-neutral) ───────────────
// These are NOT hardware PTE bits. They are translated to hardware
// format by arch_vmm_make_pte() in each arch's arch.c.

#define VMM_PRESENT (1ULL << 0)
#define VMM_WRITABLE (1ULL << 1)
#define VMM_USER (1ULL << 2)
#define VMM_CACHE_DISABLE (1ULL << 3)
#define VMM_NO_EXECUTE (1ULL << 4)

// ─── Paging Geometry (architecture-neutral, 4-level / 4 KiB granule) ──
// Single source of truth for the page-table walk math, instead of the shift/mask
// literals it replaces being hand-copied at every call site in vmm.c.

#define VMM_LEVEL_BITS 9                                // index bits per page-table level
#define VMM_LEVELS 4                                    // page-table depth (root..leaf)
#define VMM_VA_BITS (12 + VMM_LEVELS * VMM_LEVEL_BITS)  // usable virtual-address width (48)

// Canonical split between the per-context lower half and the shared kernel upper half.
#define VMM_ADDR_SPLIT (1ULL << (VMM_VA_BITS - 1))

// ─── VMM Context ─────────────────────────────────────────────

struct vmm_context {
    uint64_t root_phys;       // Physical address of root page table
    uint64_t *root_virt;      // HHDM virtual address of root page table
    uint64_t demand_page_lo;  // Start of this context's demand-pageable range
    uint64_t demand_page_hi;  // End (exclusive) of the demand-pageable range (0/0 = disabled)
};

extern struct vmm_context *vmm_kernel_context;
extern struct vmm_context *vmm_current_context;

// ─── VMM API ─────────────────────────────────────────────────

// Initialize VMM: capture Limine's page tables as kernel context
void vmm_init(void);

// Create a new address space (clones kernel upper-half entries)
struct vmm_context *vmm_create_context(void);

// Destroy an address space (frees the entire lower-half page-table tree + context struct)
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

#endif  // PROS_VMM_H
