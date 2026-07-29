#include "kernel.h"

#include "arch.h"
#include "fdt.h"
#include "mem.h"
#include "string.h"

#include <stdarg.h>

extern char _kernel_end[];

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
    kprintf("%s\n", ARCH_NAME);
    kprintf("Hardware thread %d\n", hart_id);

    uintptr_t ram_base = 0;
    size_t ram_size = 0;
    kprintf("Parse the Flattened Device Tree at %p to get memory infos\n", fdt_ptr);
    if (fdt_get_memory(fdt_ptr, &ram_base, &ram_size) == 0) {
        kprintf("Found %uMB based at %p\n", ram_size / 1024 / 1024, ram_base);
    } else {
        kpanic("Error parsing Flattened Device Tree");
    }

    uintptr_t heap_start = (uintptr_t) _kernel_end;
    uintptr_t heap_end = ram_base + ram_size;
    kprintf("Init heap memory pages free list between %p and %p\n", heap_start, heap_end);
    size_t pages = kmem_page_init(heap_start, heap_end);
    kprintf("%u pages of heap memory init between %p and %p\n", pages, heap_start, heap_end);

    void *p1 = kmem_page_alloc();
    void *p2 = kmem_page_alloc();
    kprintf("p1=%p\n", p1);
    kprintf("p2=%p\n", p2);
    kmem_page_free(p1);
    kmem_page_free(p2);

#if defined(__riscv)
    kprintf("Testing RISC-V trap handler...\n");
    asm volatile("ecall");
    kprintf("Returned from ecall successfully!\n");
#endif
#if defined(__arm__)
    kprintf("Testing ARM trap handler...\n");
    asm volatile("svc #0");
    kprintf("Returned from ARM SVC successfully!\n");
#endif

    kprintf("Wait for interrupt\n");
    while (1) {
        arch_idle();
    }

    return 0;
}
