#include "boot.h"

#include "arch.h"
#include "fb.h"
#include "kprintf.h"
#include "pmm.h"
#include "heap.h"

void _start(void) {
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        arch_halt();
    }

    arch_init();

    fb_init(framebuffer_request.response);
    kprintf("PjErOS\n");

    size_t pages = pmm_init(hhdm_request.response, memmap_request.response);
    kprintf("Initialized %zu pages of %zu B for a total of %zu MB kernel heap\n", pages, PAGE_SIZE, pages * PAGE_SIZE / 1024 / 1024);

    heap_init();

    // Test small allocation
    // char *buf1 = kmalloc(32);
    // kprintf("buf1 allocated 32B at %p\n", buf1);
    // Test multi-page allocation
    // char *buf2 = kmalloc(8000);
    // kprintf("buf2 allocated 8000B at %p\n", buf2);
    // Test small allocation again
    // char *buf3 = kmalloc(32);
    // kprintf("buf3 allocated at 32B %p\n", buf3);
    // Test multi-page allocation again
    // char *buf4 = kmalloc(8000);
    // kprintf("buf4 allocated at 8000B %p\n", buf4);

    if (dtb_request.response) {
        kprintf("Device Tree Binary is at %p\n", dtb_request.response->dtb_ptr);
    } else if (rsdp_request.response) {
        kprintf("Root System Description Pointer is at %p\n", rsdp_request.response->address);
    }

    kprintf("Halt\n");
    arch_halt();
}
