#include "arch.h"
#include "fdt.h"
#include "mem.h"
#include "string.h"

#include <stdarg.h>

int kprintf(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    uart_puts(buf);
    return len;
}

void kpanic(const char *msg) {
    kprintf("PANIC: %s\nkernel execution aborted\n", msg);
}

/*
 * Architecture-agnostic main kernel entry point.
 */
int kmain(const uint32_t hart_id, const void *fdt_ptr) {
    kprintf("PjErOS\n");
    kprintf("%s\n", ARCH_NAME, hart_id);
    kprintf("Hardware thread %d\n", hart_id);

    // inside kmain:
    uint64_t ram_base = 0;
    uint64_t ram_size = 0;

    kprintf("Parse the Flattened Device Tree at %p to get memory infos\n", fdt_ptr);
    if (fdt_get_memory(fdt_ptr, &ram_base, &ram_size) == 0) {
        kprintf("Found %uMB based at %p\n", (uint32_t) (ram_size / 1024 / 1024), (uint32_t) ram_base);
    } else {
        kpanic("Error parsing Flattened Device Tree");
    }

    kmem_init((char *) ram_base, (size_t) ram_size);

    char *p1 = kmem_alloc();
    char *p2 = kmem_alloc();

    kmem_free(p1);
    kmem_free(p2);

    while (10000000) {
        arch_idle();
    }
    // shutdown();

    return 0;
}
