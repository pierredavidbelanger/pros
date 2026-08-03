#include "arch.h"
#include "boot.h"
#include "fb.h"
#include "kprintf.h"

void kmain(void) {
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        khcf();
    }

    arch_init();
    fb_init(framebuffer_request.response);

    kprintf("Welcome to PjErOS!");

    khcf();
}
