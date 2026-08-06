#include "mm/vmm.h"

#include "arch/arch.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "core/memory.h"
#include "core/kprintf.h"

// The 4-level page table walk is identical for x86_64 and AArch64 (4K granule).
// All architecture differences are handled by arch_vmm_*() functions.

struct vmm_context *vmm_kernel_context;
struct vmm_context *vmm_current_context;

void vmm_init(void) {
    vmm_kernel_context = kmalloc(sizeof(struct vmm_context));

    uint64_t user_root = arch_vmm_get_user_root();
    if (user_root == 0) {
        user_root = pmm_alloc(1);
        uint64_t *user_virt = pmm_phys_to_virt(user_root);
        memset(user_virt, 0, PAGE_SIZE);
        arch_vmm_set_user_root(user_root);
    }

    vmm_kernel_context->root_phys = user_root;
    vmm_kernel_context->root_virt = pmm_phys_to_virt(user_root);
    vmm_current_context = vmm_kernel_context;
}

struct vmm_context *vmm_create_context(void) {
    uint64_t root_phys = pmm_alloc(1);
    if (!root_phys) return NULL;

    uint64_t *root_virt = pmm_phys_to_virt(root_phys);
    memset(root_virt, 0, PAGE_SIZE);

    // For single-root architectures (x86_64), clone upper-half entries (256..511).
    // For dual-root architectures (AArch64), upper half is managed separately via TTBR1_EL1,
    // so root_virt[256..511] is simply unused/ignored for lower-half TTBR0_EL1 tables.
    uint64_t kroot_phys = arch_vmm_get_kernel_root();
    uint64_t *kroot_virt = pmm_phys_to_virt(kroot_phys);
    for (size_t i = 256; i < 512; i++) {
        root_virt[i] = kroot_virt[i];
    }

    struct vmm_context *ctx = kmalloc(sizeof(struct vmm_context));
    ctx->root_phys = root_phys;
    ctx->root_virt = root_virt;
    return ctx;
}

void vmm_destroy_context(struct vmm_context *ctx) {
    if (!ctx) return;
    if (ctx->root_phys) {
        pmm_free(ctx->root_phys, 1);
    }
    kfree(ctx);
}

// Selects the proper root page table:
// Lower half addresses (< 0x800000000000) → ctx->root_virt (CR3 lower half / TTBR0_EL1)
// Upper half addresses (>= 0x800000000000) → Kernel root (CR3 upper half / TTBR1_EL1)
static uint64_t *get_root_table(struct vmm_context *ctx, uint64_t virt_addr) {
    if (virt_addr >= 0x800000000000ULL) {
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

int vmm_map_page(struct vmm_context *ctx, uint64_t virt_addr,
                 uint64_t phys_addr, uint64_t flags) {
    if (!ctx) return -1;

    uint64_t *root = get_root_table(ctx, virt_addr);
    if (!root) return -1;

    size_t l4_idx = (virt_addr >> 39) & 0x1FF;
    size_t l3_idx = (virt_addr >> 30) & 0x1FF;
    size_t l2_idx = (virt_addr >> 21) & 0x1FF;
    size_t l1_idx = (virt_addr >> 12) & 0x1FF;

    uint64_t *l3 = get_or_create_table(root, l4_idx);
    if (!l3) return -1;

    uint64_t *l2 = get_or_create_table(l3, l3_idx);
    if (!l2) return -1;

    uint64_t *l1 = get_or_create_table(l2, l2_idx);
    if (!l1) return -1;

    // Write leaf page entry
    l1[l1_idx] = arch_vmm_make_pte(phys_addr, flags | VMM_PRESENT,
                                    false /* is_table → leaf page */);

    arch_vmm_invlpg((void *)virt_addr);
    return 0;
}

int vmm_unmap_page(struct vmm_context *ctx, uint64_t virt_addr) {
    if (!ctx) return -1;

    uint64_t *root = get_root_table(ctx, virt_addr);
    if (!root) return -1;

    size_t l4_idx = (virt_addr >> 39) & 0x1FF;
    size_t l3_idx = (virt_addr >> 30) & 0x1FF;
    size_t l2_idx = (virt_addr >> 21) & 0x1FF;
    size_t l1_idx = (virt_addr >> 12) & 0x1FF;

    if (!arch_vmm_pte_is_present(root[l4_idx])) return -1;
    uint64_t *l3 = pmm_phys_to_virt(arch_vmm_pte_get_phys(root[l4_idx]));

    if (!arch_vmm_pte_is_present(l3[l3_idx])) return -1;
    uint64_t *l2 = pmm_phys_to_virt(arch_vmm_pte_get_phys(l3[l3_idx]));

    if (!arch_vmm_pte_is_present(l2[l2_idx])) return -1;
    uint64_t *l1 = pmm_phys_to_virt(arch_vmm_pte_get_phys(l2[l2_idx]));

    if (!arch_vmm_pte_is_present(l1[l1_idx])) return -1;

    l1[l1_idx] = 0;
    arch_vmm_invlpg((void *)virt_addr);
    return 0;
}

uint64_t vmm_virt_to_phys(struct vmm_context *ctx, uint64_t virt_addr) {
    if (!ctx) return 0;

    uint64_t *root = get_root_table(ctx, virt_addr);
    if (!root) return 0;

    size_t l4_idx = (virt_addr >> 39) & 0x1FF;
    size_t l3_idx = (virt_addr >> 30) & 0x1FF;
    size_t l2_idx = (virt_addr >> 21) & 0x1FF;
    size_t l1_idx = (virt_addr >> 12) & 0x1FF;

    if (!arch_vmm_pte_is_present(root[l4_idx])) return 0;
    uint64_t *l3 = pmm_phys_to_virt(arch_vmm_pte_get_phys(root[l4_idx]));

    if (!arch_vmm_pte_is_present(l3[l3_idx])) return 0;
    uint64_t *l2 = pmm_phys_to_virt(arch_vmm_pte_get_phys(l3[l3_idx]));

    if (!arch_vmm_pte_is_present(l2[l2_idx])) return 0;
    uint64_t *l1 = pmm_phys_to_virt(arch_vmm_pte_get_phys(l2[l2_idx]));

    if (!arch_vmm_pte_is_present(l1[l1_idx])) return 0;

    return arch_vmm_pte_get_phys(l1[l1_idx]) | (virt_addr & 0xFFF);
}

void vmm_switch_context(struct vmm_context *ctx) {
    if (ctx && ctx->root_phys) {
        vmm_current_context = ctx;
        arch_vmm_set_user_root(ctx->root_phys);
    }
}

bool vmm_handle_page_fault(uint64_t fault_addr, uint64_t error_code) {
    uint64_t hhdm_base = pmm_get_hhdm_offset();

    // Case 1: HHDM Higher-Half MMIO Access Fault (e.g. PCIe ECAM at 0xE0000000)
    // HHDM range is [hhdm_base, hhdm_base + 4TB). Excludes kernel text at 0xffffffff80000000.
    if (hhdm_base != 0 && fault_addr >= hhdm_base && fault_addr < (hhdm_base + 0x40000000000ULL)) {
        uint64_t phys_addr = fault_addr - hhdm_base;
        uint64_t aligned_virt = fault_addr & ~0xFFFULL;
        uint64_t aligned_phys = phys_addr & ~0xFFFULL;

        int res = vmm_map_page(vmm_kernel_context, aligned_virt, aligned_phys, VMM_WRITABLE);
        if (res == 0) {
            return true;
        }
        return false;
    }

    // Case 2: Lower-Half User Space Demand Paging Fault (0x60000000 test region)
    if (fault_addr >= 0x60000000ULL && fault_addr < 0x70000000ULL) {
        uint64_t phys_addr = pmm_alloc(1);
        if (!phys_addr) return false;

        void *virt_addr = pmm_phys_to_virt(phys_addr);
        memset(virt_addr, 0, PAGE_SIZE);

        int res = vmm_map_page(vmm_current_context,
                               fault_addr & ~0xFFFULL,
                               phys_addr,
                               VMM_WRITABLE | VMM_USER);
        if (res != 0) {
            pmm_free(phys_addr, 1);
            return false;
        }

        kprintf("[VMM  ] Demand Paging: mapped virt %p -> phys %p\n",
                (void *)(fault_addr & ~0xFFFULL), (void *)phys_addr);
        return true;
    }

    return false;
}
