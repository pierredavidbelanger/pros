#include "vmm.h"

#include "arch.h"
#include "pmm.h"
#include "heap.h"
#include "memory.h"

// Surprisingly, this VMM stuff is NOT arch specific.
// The 4-level tree walk logic is identical for both x86_64 and AArch64!

struct vmm_context *vmm_kernel_context;
struct vmm_context *vmm_current_context;

void vmm_init(void) {
    vmm_kernel_context = kmalloc(sizeof(struct vmm_context));
    // Arch specific current root
    vmm_kernel_context->root_phys = (uint64_t *) arch_vmm_get_root();
    vmm_kernel_context->root_virt = pmm_phys_to_virt((uint64_t) vmm_kernel_context->root_phys);
    vmm_current_context = vmm_kernel_context;
}

struct vmm_context *vmm_create_context(void) {
    uint64_t root_phys = pmm_alloc(1);
    if (!root_phys) return NULL;

    uint64_t *root_virt = pmm_phys_to_virt(root_phys);
    memset(root_virt, 0, PAGE_SIZE);

    // Copy kernel upper-half mapping (entries 256..511)
    for (size_t i = 256; i < 512; i++) {
        root_virt[i] = vmm_kernel_context->root_virt[i];
    }

    struct vmm_context *ctx = kmalloc(sizeof(struct vmm_context));
    ctx->root_phys = (uint64_t *) root_phys;
    ctx->root_virt = root_virt;
    return ctx;
}

void vmm_destroy_context(struct vmm_context *ctx) {
    if (!ctx) return;
    if (ctx->root_phys) {
        pmm_free((uint64_t) ctx->root_phys, 1);
    }
    kfree(ctx);
}

static uint64_t *get_next_table(uint64_t *current_table, size_t index) {
    if (arch_vmm_get_flags(current_table[index], VMM_PRESENT)) {
        // Extract physical address of next table (clear lower 12 permission bits)
        uint64_t next_phys = current_table[index] & ~0xFFFULL;
        return pmm_phys_to_virt(next_phys);
    }

    // Allocate missing table frame
    uint64_t next_phys = pmm_alloc(1);
    if (!next_phys) {
        return NULL; // Out of physical memory
    }

    uint64_t *next_virt = pmm_phys_to_virt(next_phys);
    memset(next_virt, 0, PAGE_SIZE);

    // Set parent entry pointing to new table frame with full intermediate permissions
    current_table[index] = arch_vmm_set_flags(next_phys, VMM_PRESENT | VMM_WRITABLE | VMM_USER);
    return next_virt;
}

int vmm_map_page(struct vmm_context *ctx, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    if (!ctx || !ctx->root_virt) {
        return -1; // Invalid context
    }

    // Extract indices for all 4 table levels
    uint64_t l4_idx = (virt_addr >> 39) & 0x1FF;
    uint64_t l3_idx = (virt_addr >> 30) & 0x1FF;
    uint64_t l2_idx = (virt_addr >> 21) & 0x1FF;
    uint64_t l1_idx = (virt_addr >> 12) & 0x1FF;

    // Walk down the page tree, allocating intermediate tables if missing
    uint64_t *l3 = get_next_table(ctx->root_virt, l4_idx);
    if (!l3) return -1;

    uint64_t *l2 = get_next_table(l3, l3_idx);
    if (!l2) return -1;

    uint64_t *l1 = get_next_table(l2, l2_idx);
    if (!l1) return -1;

    // Write leaf entry at Level 1
    l1[l1_idx] = arch_vmm_set_flags(phys_addr & ~0xFFFULL, flags | VMM_PRESENT);

    // Arch specific flush TLB entry for this virtual address
    arch_vmm_invlpg((void *) virt_addr);

    return 0;
}

int vmm_unmap_page(struct vmm_context *ctx, uint64_t virt_addr) {
    if (!ctx || !ctx->root_virt) return -1;

    uint64_t l4_idx = (virt_addr >> 39) & 0x1FF;
    uint64_t l3_idx = (virt_addr >> 30) & 0x1FF;
    uint64_t l2_idx = (virt_addr >> 21) & 0x1FF;
    uint64_t l1_idx = (virt_addr >> 12) & 0x1FF;

    if (!arch_vmm_get_flags(ctx->root_virt[l4_idx], VMM_PRESENT)) return -1;
    uint64_t *l3 = pmm_phys_to_virt(ctx->root_virt[l4_idx] & ~0xFFFULL);

    if (!arch_vmm_get_flags(l3[l3_idx], VMM_PRESENT)) return -1;
    uint64_t *l2 = pmm_phys_to_virt(l3[l3_idx] & ~0xFFFULL);

    if (!arch_vmm_get_flags(l2[l2_idx], VMM_PRESENT)) return -1;
    uint64_t *l1 = pmm_phys_to_virt(l2[l2_idx] & ~0xFFFULL);

    if (!arch_vmm_get_flags(l1[l1_idx], VMM_PRESENT)) return -1;

    // Clear leaf mapping
    l1[l1_idx] = 0;

    // Arch specific flush TLB entry for this virtual address
    arch_vmm_invlpg((void *) virt_addr);

    return 0;
}

uint64_t vmm_virt_to_phys(struct vmm_context *ctx, uint64_t virt_addr) {
    if (!ctx || !ctx->root_virt) return 0;

    uint64_t l4_idx = (virt_addr >> 39) & 0x1FF;
    uint64_t l3_idx = (virt_addr >> 30) & 0x1FF;
    uint64_t l2_idx = (virt_addr >> 21) & 0x1FF;
    uint64_t l1_idx = (virt_addr >> 12) & 0x1FF;

    if (!arch_vmm_get_flags(ctx->root_virt[l4_idx], VMM_PRESENT)) return 0;
    uint64_t *l3 = pmm_phys_to_virt(ctx->root_virt[l4_idx] & ~0xFFFULL);

    if (!arch_vmm_get_flags(l3[l3_idx], VMM_PRESENT)) return 0;
    uint64_t *l2 = pmm_phys_to_virt(l3[l3_idx] & ~0xFFFULL);

    if (!arch_vmm_get_flags(l2[l2_idx], VMM_PRESENT)) return 0;
    uint64_t *l1 = pmm_phys_to_virt(l2[l2_idx] & ~0xFFFULL);

    if (!arch_vmm_get_flags(l1[l1_idx], VMM_PRESENT)) return 0;

    uint64_t phys_page = l1[l1_idx] & ~0xFFFULL;
    return phys_page | (virt_addr & 0xFFFULL);
}

void vmm_switch_context(struct vmm_context *ctx) {
    if (ctx && ctx->root_phys) {
        // Track current context
        vmm_current_context = ctx;
        // Arch specific to set the current root (effectively switching context)
        arch_vmm_set_root((uint64_t) ctx->root_phys);
    }
}
