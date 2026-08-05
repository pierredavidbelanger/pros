#include "boot.h"

#include "arch.h"
#include "fb.h"
#include "kprintf.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"

void test_pmm(void);

void test_heap(void);

void test_vmm_context(void);
void test_vmm_page_fault(void);

void _start(void) {
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        arch_halt();
    }

    arch_init();

    fb_init(framebuffer_request.response);
    kprintf("[FB  ] Initialized the framebuffer, ready to kprintf\n");
    kprintf("[K   ] Wellcome to PjErOS!\n");

    size_t pages = pmm_init(hhdm_request.response, memmap_request.response);
    kprintf("[PMM ] Initialized PMM, ready to alloc/free physical pages\n");
    kprintf("[PMM ] PMM manage %zu pages of %zu B for a total of %zu MB kernel heap available\n", pages, PAGE_SIZE, pages * PAGE_SIZE / 1024 / 1024);
    test_pmm();

    heap_init();
    kprintf("[HEAP] Initialized, ready to kmalloc/kfree dynamic virtual memory block\n");
    test_heap();

    vmm_init();
    kprintf("[PMM ] Initialized VMM, ready to create/switch context and map/unmap pages\n");
    kprintf("[PMM ] VMM kernel context at phys:%p virt:%p\n", vmm_kernel_context->root_phys, vmm_kernel_context->root_virt);
    //test_vmm_context();
    test_vmm_page_fault();

    if (dtb_request.response) {
        kprintf("[DTB ] Device Tree Binary is at %p\n", dtb_request.response->dtb_ptr);
    } else if (rsdp_request.response) {
        kprintf("[RSDP] Root System Description Pointer is at %p\n", rsdp_request.response->address);
    }

    kprintf("[K   ] Halt\n");
    arch_halt();
}

void test_pmm(void) {
    uint64_t one_page = pmm_alloc(1);
    kprintf("[PMM ] Got one page at %p\n", one_page);
    pmm_free(one_page, 1);
    uint64_t two_page = pmm_alloc(2);
    kprintf("[PMM ] Got two pages at %p\n", two_page);
    pmm_free(two_page, 2);
}

void test_heap(void) {
    // Test small allocation
    char *buf1 = kmalloc(32);
    kprintf("[HEAP] buf1 allocated 32B at %p\n", buf1);
    // Test multi-page allocation
    char *buf2 = kmalloc(8000);
    kprintf("[HEAP] buf2 allocated 8000B at %p\n", buf2);
    // Test small allocation again
    char *buf3 = kmalloc(32);
    kprintf("[HEAP] buf3 allocated at 32B %p\n", buf3);
    // Test multi-page allocation again
    char *buf4 = kmalloc(8000);
    kprintf("[HEAP] buf4 allocated at 8000B %p\n", buf4);
    kfree(buf1);
    kfree(buf2);
    kfree(buf3);
    kfree(buf4);
}

void test_vmm_context(void) {
    // Test Page Mapping & Translation
    uint64_t test_phys = pmm_alloc(1);
    uint64_t test_virt = 0x40000000ULL; // Lower-half test virtual address
    if (vmm_map_page(vmm_kernel_context, test_virt, test_phys, VMM_WRITABLE) == 0) {
        kprintf("[VMM ] Successfully mapped virt %p -> phys %p\n", (void *) test_virt, (void *) test_phys);
        uint64_t translated = vmm_virt_to_phys(vmm_kernel_context, test_virt);
        kprintf("[VMM ] vmm_virt_to_phys(%p) = %p\n", (void *) test_virt, (void *) translated);
        // Write magic value through virtual pointer
        *(volatile uint32_t *) test_virt = 0xDEADBEEF;
        // Read magic value from physical memory via HHDM
        uint32_t *hhdm_ptr = pmm_phys_to_virt(test_phys);
        kprintf("[VMM ] Value written at virt %p reads as 0x%X at phys HHDM %p\n", (void *) test_virt, *hhdm_ptr, hhdm_ptr);
    }
    // Test Unmapping
    if (vmm_unmap_page(vmm_kernel_context, test_virt) == 0) {
        kprintf("[VMM ] Successfully unmapped virt %p\n", (void *) test_virt);
        uint64_t unmapped_phys = vmm_virt_to_phys(vmm_kernel_context, test_virt);
        kprintf("[VMM ] vmm_virt_to_phys(%p) after unmap = %p (Expected: 0)\n", (void *) test_virt, (void *) unmapped_phys);
    }
    // Free the test frame back to PMM
    pmm_free(test_phys, 1);
    // Test Context Creation & Isolation
    struct vmm_context *user_ctx = vmm_create_context();
    kprintf("[VMM ] Created user context at Phys: %p, Virt: %p\n", user_ctx->root_phys, user_ctx->root_virt);
    uint64_t user_phys = pmm_alloc(1);
    uint64_t user_virt = 0x50000000ULL; // User virtual address
    // Map in user context only
    vmm_map_page(user_ctx, user_virt, user_phys, VMM_WRITABLE | VMM_USER);
    // Verify lookup in user_ctx vs kernel_ctx
    uint64_t in_user = vmm_virt_to_phys(user_ctx, user_virt);
    uint64_t in_kernel = vmm_virt_to_phys(vmm_kernel_context, user_virt);
    kprintf("[VMM ] Lookup in user_ctx: %p (Expected: %p)\n", (void *) in_user, (void *) user_phys);
    kprintf("[VMM ] Lookup in kernel_ctx: %p (Expected: 0)\n", (void *) in_kernel);
    // Test Context Switching
    vmm_switch_context(user_ctx);
    kprintf("[VMM ] Switched to user_ctx:%p (vmm_current_context:%p)\n", user_ctx, vmm_current_context);
    // Access mapped memory under user_ctx
    *(volatile uint32_t *) user_virt = 0xCAFEBABE;
    uint32_t *user_hhdm = pmm_phys_to_virt(user_phys);
    kprintf("[VMM ] Value written in user_ctx at virt %p reads as 0x%X at phys HHDM %p\n", (void *) user_virt, *user_hhdm, user_hhdm);
    // Switch back to kernel context
    vmm_switch_context(vmm_kernel_context);
    kprintf("[VMM ] Switched back to kernel_ctx:%p (vmm_current_context:%p)\n", vmm_kernel_context, vmm_current_context);
    // Cleanup user context
    vmm_unmap_page(user_ctx, user_virt);
    pmm_free(user_phys, 1);
    vmm_destroy_context(user_ctx);
}

void test_vmm_page_fault(void) {
    kprintf("\n[VMM ] Testing Demand Paging...\n");
    struct vmm_context *user_ctx = vmm_create_context();
    vmm_switch_context(user_ctx);
    // Address range 0x60000000..0x60004000 is completely UNMAPPED right now!
    uint64_t unmapped_page0 = 0x60000000ULL;
    uint64_t unmapped_page1 = 0x60001000ULL;
    // Verify both pages are NOT present in VMM
    kprintf("[VMM ] Page 0 Before access: virt %p phys = %p (Expected: 0)\n", (void *) unmapped_page0, (void *) vmm_virt_to_phys(user_ctx, unmapped_page0));
    kprintf("[VMM ] Page 1 Before access: virt %p phys = %p (Expected: 0)\n", (void *) unmapped_page1, (void *) vmm_virt_to_phys(user_ctx, unmapped_page1));
    // Touch Page 1 FIRST (out of order!) -> Triggers Page Fault #1
    kprintf("[VMM ] Writing to Page 1 at %p (unmapped!)...\n", (void *) (unmapped_page1 + 0x500));
    // Triggers page fault
    *(volatile uint32_t *)(unmapped_page1 + 0x500) = 0x88888888;
    // Verify Page 1 is now allocated, but Page 0 is STILL 0!
    kprintf("[VMM ] Value written at %p reads as 0x%X\n", (void *)(unmapped_page1 + 0x500), *(volatile uint32_t *)(unmapped_page1 + 0x500));
    kprintf("[VMM ] Page 1 phys = %p\n", (void *) vmm_virt_to_phys(user_ctx, unmapped_page1));
    kprintf("[VMM ] Page 0 phys = %p (Expected: 0)\n", (void *) vmm_virt_to_phys(user_ctx, unmapped_page0));
    // Touch Page 0 SECOND -> Triggers Page Fault #2
    kprintf("[VMM ] Writing to Page 0 at %p (unmapped!)...\n", (void *) unmapped_page0);
    // Triggers page fault
    *(volatile uint32_t *)unmapped_page0 = 0x11111111;
    // Verify Page 0 is now allocated too!
    kprintf("[VMM ] Value written at %p reads as 0x%X\n", (void *) unmapped_page0, *(volatile uint32_t *)unmapped_page0);
    kprintf("[VMM ] Page 0 phys = %p\n", (void *) vmm_virt_to_phys(user_ctx, unmapped_page0));
    kprintf("[VMM ] Demand Paging Test PASSED!\n");
    vmm_destroy_context(user_ctx);
}
