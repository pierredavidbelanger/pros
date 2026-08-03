#include "boot.h"

#include "arch.h"
#include "fb.h"
#include "kprintf.h"

void _start(void) {
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        khcf();
    }

    arch_init();
    fb_init(framebuffer_request.response);

    kprintf("PjErOS\n");
    if (dtb_request.response) {
        kprintf("Device Tree Binary is at %p\n", dtb_request.response->dtb_ptr);
    } else if (rsdp_request.response) {
        kprintf("Root System Description Pointer is at %p\n", rsdp_request.response->address);
    }

    kprintf("Halt and Catch in Fire\n");
    khcf();
}
