#include "mm/vmm.h"

#include "arch/arch.h"
#include "core/boot.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "core/memory.h"
#include "core/kprintf.h"

// The 4-level page table walk is identical for x86_64 and AArch64 (4K granule).
// All architecture differences are handled by arch_vmm_*() functions. The level geometry
// itself (VMM_LEVEL_BITS, VMM_LEVELS, VMM_VA_BITS, VMM_ADDR_SPLIT) lives in vmm.h as the
// single source of truth; VMM_INDEX() below is the one place it's applied to a walk.

// Index of virt_addr within the page-table level `level` levels above the leaf
// (level 3 = root/top level, level 0 = leaf/PTE level).
#define VMM_INDEX(virt_addr, level) (((virt_addr) >> (12 + (level) * VMM_LEVEL_BITS)) & ((1ULL << VMM_LEVEL_BITS) - 1))

struct vmm_context *vmm_kernel_context;
struct vmm_context *vmm_current_context;

void vmm_init(void) {
    // Assert the bootloader actually gave us the paging mode this walker is built for.
    // Limine reports 4-level paging as mode 0 on both x86_64 and AArch64.
    if (!paging_mode_request.response || paging_mode_request.response->mode != paging_mode_request.min_mode) {
        kpanic("[VMM  ] bootloader returned an unsupported paging mode.");
    }
    kprintf("VMM", "Paging mode: %lu (%d-level)\n", paging_mode_request.response->mode, VMM_LEVELS);

    vmm_kernel_context = kmalloc(sizeof(struct vmm_context));
    if (!vmm_kernel_context) {
        kpanic("Cant alloc the kernel context");
    }

    // arch_vmm_ensure_user_root() hides a per-architecture difference:
    //
    // on x86_64 (single-root: CR3) it just returns what Limine already set up.
    // on AArch64 (dual-root: TTBR0_EL1 / TTBR1_EL1) Limine leaves TTBR0_EL1 unset, so it allocates a fresh, blank table.
    //
    // That means vmm_kernel_context holds the real, complete kernel table on x86_64.
    // but a near-empty placeholder on AArch64, and its real mapping live in TTBR1_EL1, reachable via arch_vmm_get_kernel_root().

    uint64_t user_root = arch_vmm_ensure_user_root();
    if (!user_root) {
        kpanic("Cant alloc the user root page table");
    }
    vmm_kernel_context->root_phys = user_root;
    vmm_kernel_context->root_virt = pmm_phys_to_virt(user_root);
    vmm_kernel_context->demand_page_lo = 0;
    vmm_kernel_context->demand_page_hi = 0;
    vmm_current_context = vmm_kernel_context;
}

struct vmm_context *vmm_create_context(void) {
    // arch_vmm_new_context_root() owns whatever architecture-specific setup a fresh context needs to see the kernel's mappings
    // (e.g. cloning shared entries on a single-root architecture and nothing to do on a dual-root one).
    uint64_t root_phys = arch_vmm_new_context_root(arch_vmm_get_kernel_root());
    if (!root_phys) return NULL;

    struct vmm_context *ctx = kmalloc(sizeof(struct vmm_context));
    if (!ctx) {
        pmm_free(root_phys, 1);
        return NULL;
    }
    ctx->root_phys = root_phys;
    ctx->root_virt = pmm_phys_to_virt(root_phys);
    ctx->demand_page_lo = 0;
    ctx->demand_page_hi = 0;
    return ctx;
}

// Recursively frees every present page below `table` (a page-table page `level` levels above the leaf),
// without freeing table itself, the caller owns that page.
// At level 0, entries point directly to physical data frames,
// above that, entries point to further table pages which must be emptied (recursively) before being freed themselves.
static void vmm_free_table_recursive(uint64_t *table, int level) {
    for (size_t i = 0; i < 512; i++) {
        if (!arch_vmm_pte_is_present(table[i])) {
            continue;
        }
        uint64_t child_phys = arch_vmm_pte_get_phys(table[i]);
        if (level > 0) {
            vmm_free_table_recursive(pmm_phys_to_virt(child_phys), level - 1);
        }
        pmm_free(child_phys, 1);
    }
}

void vmm_destroy_context(struct vmm_context *ctx) {
    if (!ctx) return;
    if (ctx->root_phys) {
        // Walk indices 0..255 only (the lower half). 256..511 are the shared/cloned kernel
        // entries installed by vmm_create_context() and must never be freed here.
        for (size_t i = 0; i < 256; i++) {
            if (!arch_vmm_pte_is_present(ctx->root_virt[i])) {
                continue;
            }
            uint64_t child_phys = arch_vmm_pte_get_phys(ctx->root_virt[i]);
            vmm_free_table_recursive(pmm_phys_to_virt(child_phys), VMM_LEVELS - 2);
            pmm_free(child_phys, 1);
        }
        pmm_free(ctx->root_phys, 1);
    }
    kfree(ctx);
}

// Selects the proper root page table:
// Lower half addresses (< VMM_ADDR_SPLIT) → ctx->root_virt (CR3 lower half / TTBR0_EL1)
// Upper half addresses (>= VMM_ADDR_SPLIT) → Kernel root (CR3 upper half / TTBR1_EL1)
static uint64_t *get_root_table(struct vmm_context *ctx, uint64_t virt_addr) {
    if (virt_addr >= VMM_ADDR_SPLIT) {
        uint64_t kroot_phys = arch_vmm_get_kernel_root();
        return pmm_phys_to_virt(kroot_phys);
    }
    return ctx->root_virt;
}

// Walk one level of the page table. If the entry at [index] is not present,
// allocate a new table page and install it as an intermediate table descriptor.
static uint64_t *get_or_create_table(uint64_t *table, size_t index) {
    if (arch_vmm_pte_is_present(table[index])) {
        uint64_t child_phys = arch_vmm_pte_get_phys(table[index]);
        return pmm_phys_to_virt(child_phys);
    }

    // Allocate a new page for the child table
    uint64_t child_phys = pmm_alloc(1);
    if (!child_phys) return NULL;

    uint64_t *child_virt = pmm_phys_to_virt(child_phys);
    memset(child_virt, 0, PAGE_SIZE);

    // Install as intermediate table descriptor (VMM_PRESENT | VMM_WRITABLE | VMM_USER)
    table[index] = arch_vmm_make_pte(child_phys,
                                     VMM_PRESENT | VMM_WRITABLE | VMM_USER,
                                     true /* is_table */);
    return child_virt;
}

// Walk from the root down to the table holding virt_addr's leaf PTE,
// and return a pointer to that leaf slot.
// With create=true (vmm_map_page), missing intermediate tables are allocated on the fly, so this never fails except on OOM.
// With create=false (vmm_unmap_page, vmm_virt_to_phys), a missing intermediate table means "unmapped" and this returns NULL
static uint64_t *walk_to_leaf_slot(struct vmm_context *ctx, uint64_t virt_addr, bool create) {
    uint64_t *table = get_root_table(ctx, virt_addr);
    if (!table) return NULL;

    for (int level = VMM_LEVELS - 1; level > 0; level--) {
        size_t index = VMM_INDEX(virt_addr, level);
        if (create) {
            table = get_or_create_table(table, index);
        } else if (arch_vmm_pte_is_present(table[index])) {
            table = pmm_phys_to_virt(arch_vmm_pte_get_phys(table[index]));
        } else {
            table = NULL;
        }
        if (!table) return NULL;
    }
    return &table[VMM_INDEX(virt_addr, 0)];
}

int vmm_map_page(struct vmm_context *ctx, uint64_t virt_addr,
                 uint64_t phys_addr, uint64_t flags) {
    if (!ctx) return -1;

    uint64_t *leaf = walk_to_leaf_slot(ctx, virt_addr, true /* create_missing */);
    if (!leaf) return -1;

    *leaf = arch_vmm_make_pte(phys_addr, flags | VMM_PRESENT, false /* is_table → leaf page */);

    arch_vmm_invlpg((void *)virt_addr);
    return 0;
}

int vmm_unmap_page(struct vmm_context *ctx, uint64_t virt_addr) {
    if (!ctx) return -1;

    uint64_t *leaf = walk_to_leaf_slot(ctx, virt_addr, false /* create_missing */);
    if (!leaf || !arch_vmm_pte_is_present(*leaf)) return -1;

    *leaf = 0;
    arch_vmm_invlpg((void *)virt_addr);
    return 0;
}

uint64_t vmm_virt_to_phys(struct vmm_context *ctx, uint64_t virt_addr) {
    if (!ctx) return 0;

    uint64_t *leaf = walk_to_leaf_slot(ctx, virt_addr, false /* create_missing */);
    if (!leaf || !arch_vmm_pte_is_present(*leaf)) return 0;

    return arch_vmm_pte_get_phys(*leaf) | (virt_addr & (PAGE_SIZE - 1));
}

void vmm_switch_context(struct vmm_context *ctx) {
    if (ctx && ctx->root_phys) {
        vmm_current_context = ctx;
        arch_vmm_set_user_root(ctx->root_phys);
    }
}

static bool try_handle_hhdm_mmio_fault(uint64_t fault_addr, uint64_t error_code) {
    uint64_t hhdm_base = pmm_get_hhdm_offset();
    uint64_t hhdm_top = hhdm_base + pmm_get_max_phys_addr();

    if (hhdm_base == 0 || fault_addr < hhdm_base || fault_addr >= hhdm_top) {
        return false;
    }

    // inside the range, but the error code here says we should not touch it
    if (arch_vmm_fault_is_present(error_code)) {
        kprintf("VMM", "Fault: disallowed %s access to HHDM MMIO at %p\n", arch_vmm_fault_is_write(error_code) ? "write" : "read", (void *)fault_addr);
        return false;
    }

    uint64_t phys_addr = fault_addr - hhdm_base;
    uint64_t aligned_virt = fault_addr & ~(PAGE_SIZE - 1);
    uint64_t aligned_phys = phys_addr & ~(PAGE_SIZE - 1);

    return vmm_map_page(vmm_kernel_context, aligned_virt, aligned_phys, VMM_WRITABLE | VMM_CACHE_DISABLE) == 0;
}

// Lower-Half User-Space Demand Paging,
// within the faulting context's configured demand-pageable range (struct vmm_context.demand_page_lo/hi, a 0 means "disabled").
static bool try_handle_demand_page_fault(uint64_t fault_addr, uint64_t error_code) {
    // Faults are possible before the VMM exists.
    // Decline them rather than dereferencing nothing.
    if (!vmm_current_context) return false;

    if (fault_addr < vmm_current_context->demand_page_lo || fault_addr >= vmm_current_context->demand_page_hi) {
        return false;
    }

    if (arch_vmm_fault_is_present(error_code)) {
        // The page table entry was present but the access still faulted: a real protection
        // violation (write to read-only, wrong privilege level, ...), not a legitimate
        // not-present demand-paging fault. Return false rather than panicking here directly —
        // the caller's existing unhandled-exception path dumps full register state before
        // panicking, which is strictly more useful for debugging.
        kprintf("VMM", "Fault: disallowed %s access to present page at %p\n", arch_vmm_fault_is_write(error_code) ? "write" : "read", (void *)fault_addr);
        return false;
    }

    uint64_t phys_addr = pmm_alloc(1);
    if (!phys_addr) return false;

    void *virt_addr = pmm_phys_to_virt(phys_addr);
    memset(virt_addr, 0, PAGE_SIZE);

    uint64_t aligned_virt = fault_addr & ~(PAGE_SIZE - 1);
    if (vmm_map_page(vmm_current_context, aligned_virt, phys_addr, VMM_WRITABLE | VMM_USER) != 0) {
        pmm_free(phys_addr, 1);
        return false;
    }

    kprintf("VMM", "Demand Paging: mapped virt %p -> phys %p\n", (void *)aligned_virt, (void *)phys_addr);
    return true;
}

bool vmm_handle_page_fault(uint64_t fault_addr, uint64_t error_code) {
    if (try_handle_hhdm_mmio_fault(fault_addr, error_code)) {
        return true;
    }
    return try_handle_demand_page_fault(fault_addr, error_code);
}
