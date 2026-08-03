#include "boot.h"

#include "arch.h"
#include "fb.h"
#include "pmm.h"
#include "kprintf.h"

void _start(void) {
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        arch_halt();
    }

    arch_init();

    fb_init(framebuffer_request.response);
    kprintf("PjErOS\n");

    size_t pages = pmm_init(hhdm_request.response, memmap_request.response);
    kprintf("Initialized %zu pages of %zu B for a total of %zu MB kernel heap\n", pages, PAGE_SIZE, pages * PAGE_SIZE / 1024 / 1024);

    if (dtb_request.response) {
        kprintf("Device Tree Binary is at %p\n", dtb_request.response->dtb_ptr);
    } else if (rsdp_request.response) {
        kprintf("Root System Description Pointer is at %p\n", rsdp_request.response->address);
    }

    kprintf("Halt\n");
    arch_halt();
}
