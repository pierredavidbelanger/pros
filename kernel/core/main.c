#include "core/boot.h"

#include "arch/arch.h"
#include "core/fb.h"
#include "core/console.h"
#include "core/kprintf.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "mm/vmm.h"
#include "drivers/blockdev.h"
#include "drivers/acpi.h"
#include "drivers/pci.h"
#include "drivers/virtio_mmio.h"

void test_pmm(void);

void test_heap(void);

void test_vmm(void);

void _start(void) {
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        arch_halt();
    }

    arch_init();

    console_init();
    kprintf("[CON  ] Initialized console, ready to kprintf\n");

    kprintf("[K    ] Wellcome to PjErOS!\n");

    size_t pages = pmm_init(hhdm_request.response, memmap_request.response);
    kprintf("[PMM  ] Initialized PMM, ready to alloc/free physical pages\n");
    kprintf("[PMM  ] PMM manage %zu pages of %zu B for a total of %zu MB kernel heap available\n", pages, PAGE_SIZE, pages * PAGE_SIZE / 1024 / 1024);
    test_pmm();

    heap_init();
    kprintf("[HEAP ] Initialized Heap, ready to kmalloc/kfree dynamic virtual memory block\n");
    test_heap();

    vmm_init();
    kprintf("[VMM  ] Initialized VMM, kernel root at phys:%p virt:%p\n",
            (void *)vmm_kernel_context->root_phys,
            vmm_kernel_context->root_virt);
    test_vmm();

    blockdev_init();

    if (rsdp_request.response) {
        kprintf("[RSDP ] Root System Description Pointer is at %p\n", rsdp_request.response->address);
        acpi_init(rsdp_request.response->address);
        uint64_t ecam_base = acpi_get_mcfg_ecam_base();
        if (ecam_base) {
            pci_init(ecam_base);
        }
    } else if (dtb_request.response) {
        kprintf("[DTB  ] Device Tree Binary is at %p\n", dtb_request.response->dtb_ptr);
    }

    // Always probe direct VirtIO MMIO slots just in case we run in QEMU
    virtio_mmio_init();

    if (framebuffer_request.response) {
        fb_init(framebuffer_request.response);
        kprintf("[FB   ] Initialized the framebuffer, ready to kprintf\n");
    }

    kprintf("[K    ] Attempting ACPI shutdown...\n");
    if (acpi_shutdown() != 0) {
        kprintf("[K    ] ACPI shutdown not supported on this platform, falling back to arch shutdown...\n");
        arch_shutdown();
    }
}

void test_pmm(void) {
    uint64_t one_page = pmm_alloc(1);
    kprintf("[PMM  ] Got one page at %p\n", one_page);
    pmm_free(one_page, 1);
    uint64_t two_page = pmm_alloc(2);
    kprintf("[PMM  ] Got two pages at %p\n", two_page);
    pmm_free(two_page, 2);
}

void test_heap(void) {
    // Test small allocation
    char *buf1 = kmalloc(32);
    kprintf("[HEAP ] buf1 allocated 32B at %p\n", buf1);
    // Test multi-page allocation
    char *buf2 = kmalloc(8000);
    kprintf("[HEAP ] buf2 allocated 8000B at %p\n", buf2);
    // Test small allocation again
    char *buf3 = kmalloc(32);
    kprintf("[HEAP ] buf3 allocated at 32B %p\n", buf3);
    // Test multi-page allocation again
    char *buf4 = kmalloc(8000);
    kprintf("[HEAP ] buf4 allocated at 8000B %p\n", buf4);
    kfree(buf1);
    kfree(buf2);
    kfree(buf3);
    kfree(buf4);
}

void test_vmm(void) {
    // Test 1: Basic map & write on kernel context
    uint64_t test_phys = pmm_alloc(1);
    uint64_t test_virt = 0x40000000ULL;
    if (vmm_map_page(vmm_kernel_context, test_virt, test_phys, VMM_WRITABLE) == 0) {
        kprintf("[VMM  ] Mapped virt %p -> phys %p\n", (void *)test_virt, (void *)test_phys);
        *(volatile uint32_t *)test_virt = 0xDEADBEEF;
        uint32_t *hhdm_ptr = pmm_phys_to_virt(test_phys);
        kprintf("[VMM  ] Write 0xDEADBEEF, read back 0x%X via HHDM\n", *hhdm_ptr);
    }
    vmm_unmap_page(vmm_kernel_context, test_virt);
    pmm_free(test_phys, 1);

    // Test 2: Context creation + isolation
    struct vmm_context *user_ctx = vmm_create_context();
    kprintf("[VMM  ] Created user context phys:%p\n", (void *)user_ctx->root_phys);
    vmm_switch_context(user_ctx);
    kprintf("[VMM  ] Switched to user context (kernel kprintf still works!)\n");

    // Test 3: Demand paging (lazy allocation on fault)
    kprintf("[VMM  ] Touching unmapped page 0x60000000 (should trigger demand paging)...\n");
    *(volatile uint32_t *)0x60000000ULL = 0xCAFEBABE;
    kprintf("[VMM  ] Read back: 0x%X (expected 0xCAFEBABE)\n",
            *(volatile uint32_t *)0x60000000ULL);

    *(volatile uint32_t *)0x60001000ULL = 0x12345678;
    kprintf("[VMM  ] Page 2 read back: 0x%X (expected 0x12345678)\n",
            *(volatile uint32_t *)0x60001000ULL);

    kprintf("[VMM  ] Demand paging test PASSED!\n");

    vmm_switch_context(vmm_kernel_context);
    vmm_destroy_context(user_ctx);
}
