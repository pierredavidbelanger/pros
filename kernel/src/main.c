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

    kprintf("Welcome to PjErOS!");

    khcf();
}
