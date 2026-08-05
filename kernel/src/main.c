#include "boot.h"

#include "arch.h"
#include "fb.h"
#include "kprintf.h"
#include "pmm.h"
#include "heap.h"

void test_pmm(void);

void test_heap(void);

void _start(void) {
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        arch_halt();
    }

    arch_init();

    kprintf("[K   ] Wellcome to PjErOS!\n");

    size_t pages = pmm_init(hhdm_request.response, memmap_request.response);
    kprintf("[PMM ] Initialized PMM, ready to alloc/free physical pages\n");
    kprintf("[PMM ] PMM manage %zu pages of %zu B for a total of %zu MB kernel heap available\n", pages, PAGE_SIZE, pages * PAGE_SIZE / 1024 / 1024);
    test_pmm();

    heap_init();
    kprintf("[HEAP] Initialized, ready to kmalloc/kfree dynamic virtual memory block\n");
    test_heap();

    if (dtb_request.response) {
        kprintf("[DTB ] Device Tree Binary is at %p\n", dtb_request.response->dtb_ptr);
    } else if (rsdp_request.response) {
        kprintf("[RSDP] Root System Description Pointer is at %p\n", rsdp_request.response->address);
    }

    if (framebuffer_request.response) {
        fb_init(framebuffer_request.response);
        kprintf("[FB  ] Initialized the framebuffer, ready to kprintf\n");
    }

    kprintf("[K   ] Shutting down...\n");
    arch_shutdown();
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
