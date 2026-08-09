#include "core/test/test.h"

#include "core/kprintf.h"
#include "mm/pmm.h"
#include "mm/vmm.h"

void test_vmm(void) {
    // Basic map & write on kernel context
    uint64_t test_phys = pmm_alloc(1);
    uint64_t test_virt = 0x40000000ULL;
    if (vmm_map_page(vmm_kernel_context, test_virt, test_phys, VMM_WRITABLE) == 0) {
        kprintf("VMM", "Mapped virt %p -> phys %p\n", (void *)test_virt, (void *)test_phys);
        *(volatile uint32_t *)test_virt = 0xDEADBEEF;
        uint32_t *hhdm_ptr = pmm_phys_to_virt(test_phys);
        test_report("VMM", "write via mapping, read via HHDM", *hhdm_ptr == 0xDEADBEEF);
    }
    vmm_unmap_page(vmm_kernel_context, test_virt);
    pmm_free(test_phys, 1);

    // Context creation + isolation
    size_t free_pages_before = pmm_get_free_page_count();
    struct vmm_context *user_ctx = vmm_create_context();
    test_report("VMM", "create user context", user_ctx != NULL);
    if (!user_ctx) return;
    kprintf("VMM", "Created user context phys:%p\n", (void *)user_ctx->root_phys);

    // Opt this context into demand paging over a fixed test range (disabled by default).
    user_ctx->demand_page_lo = 0x60000000ULL;
    user_ctx->demand_page_hi = 0x70000000ULL;
    vmm_switch_context(user_ctx);
    kprintf("VMM", "Switched to user context (kernel kprintf still works!)\n");

    // Demand paging (lazy allocation on fault)
    kprintf("VMM", "Touching unmapped page 0x60000000 (should trigger demand paging)...\n");
    *(volatile uint32_t *)0x60000000ULL = 0xCAFEBABE;
    uint32_t first_page_value = *(volatile uint32_t *)0x60000000ULL;

    *(volatile uint32_t *)0x60001000ULL = 0x12345678;
    uint32_t second_page_value = *(volatile uint32_t *)0x60001000ULL;

    test_report("VMM", "demand paging", first_page_value == 0xCAFEBABE && second_page_value == 0x12345678);

    // Negative test, this is not a valid demand-paging, it should fall through and panic.
    // *(volatile uint32_t *)0x80000000ULL = 0xBAADF00D;

    vmm_switch_context(vmm_kernel_context);
    vmm_destroy_context(user_ctx);

    // Context teardown must not leak the pages it allocated (root + intermediate page tables + the two demand-paged frames above).
    size_t free_pages_after = pmm_get_free_page_count();
    kprintf("VMM", "Context teardown: %zu pages before, %zu after\n", free_pages_before, free_pages_after);
    test_report("VMM", "context teardown leaks nothing", free_pages_before == free_pages_after);
}
